// Tag catch: reads the player's Pokemon storage tags, maps the tagged Pokemon to candy families through
// the GameMaster, and answers the VGP's "encounter it?" / "catch it, with which berry?" questions.
#include "tag_catch.h"
#include "batch_evolution_policy.h"
#include "pvp_data.h"
#include <atomic>
#include <cmath>
#include <map>
#include "shared_metadata.h"
#include "runtime_bootstrap.h"
#include <android/log.h>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace pgr_shared_meta;
struct Failure { const char *where; };
void require(bool ok, const char *where) { if (!ok) throw Failure{where}; }
PgrBootBinding boot{};
const PgrSharedMetadataOutput *md = nullptr;
bool initialized = false, failed = false;
uint64_t owner = 0;
template <class F> F fn(const void *p) { require(p, "missing runtime export"); return reinterpret_cast<F>(const_cast<void *>(p)); }
uint64_t clock_ms() { timespec t{}; clock_gettime(CLOCK_BOOTTIME, &t); return uint64_t(t.tv_sec) * 1000 + t.tv_nsec / 1000000; }

struct Root {
  uintptr_t h = 0;
  Root() = default;
  explicit Root(void *v) { if (v) { h = fn<uintptr_t (*)(void *, int)>(boot.runtime.gchandle_new)(v, 0); require(h, "GC root"); } }
  Root(const Root &) = delete; Root &operator=(const Root &) = delete;
  Root(Root &&r) noexcept : h(r.h) { r.h = 0; }
  ~Root() { if (h) fn<void (*)(uintptr_t)>(boot.runtime.gchandle_free)(h); }
  void *get() const { return h ? fn<void *(*)(uintptr_t)>(boot.runtime.gchandle_get_target)(h) : nullptr; }
};
void *klass(void *o) { require(o, "null object"); return fn<void *(*)(void *)>(boot.runtime.object_get_class)(o); }
void *cls(const char *ns, const char *name) {
  const auto *api = static_cast<const PgrMetaApi *>(pgrazor_runtime_metadata_api(1)); require(api, "metadata API");
  PgrMetaClassQuery q{}; q.version = 1; q.size = sizeof(q); q.name_space = ns; q.name = name; q.canonical_match = PGR_MD_MATCH_EXACT;
  PgrMetaClassResult out{}; require(api->resolve_class(&q, &out) == 0, name); return const_cast<void *>(out.klass);
}
void *method(void *k, const char *name, int count) {
  void *m = fn<void *(*)(void *, const char *, int)>(boot.meta.class_get_method_from_name)(k, name, count); require(m, name); return m;
}
void *invoke(void *m, void *o, void **args = nullptr) {
  void *ex = nullptr;
  void *v = fn<void *(*)(void *, void *, void **, void **)>(boot.runtime.runtime_invoke)(m, o, args, &ex);
  if (ex) {
    char detail[512]{};
    auto format = reinterpret_cast<void (*)(void *, char *, int)>(dlsym(boot.handle, "il2cpp_format_exception"));
    if (format) format(ex, detail, sizeof(detail));
    __android_log_print(ANDROID_LOG_ERROR, "PGRazorTagCatch", "%s: %s", fn<const char *(*)(void *)>(boot.meta.method_get_name)(m), detail);
    throw Failure{"managed call failed"};
  }
  return v;
}
void *call(void *o, const char *name, int n = 0, void **args = nullptr) { return invoke(method(klass(o), name, n), o, args); }
template <class T> T unboxed(void *o) { require(o, "boxed value"); T v{}; memcpy(&v, fn<void *(*)(void *)>(boot.runtime.object_unbox)(o), sizeof(v)); return v; }
void *field_info(void *o, const char *name) { return fn<void *(*)(void *, const char *)>(boot.meta.class_get_field_from_name)(klass(o), name); }
void *field_ref(void *o, const char *name) { void *f = field_info(o, name); require(f, name); void *v = nullptr; fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(o, f, &v); return v; }
int field_int(void *o, const char *name) { void *f = field_info(o, name); require(f, name); int v = 0; fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(o, f, &v); return v; }
// A set enum/int field, or a present message whose own `inner` field is set. Missing fields read as unset.
bool field_set(void *o, const char *name, const char *inner) {
  void *f = o ? field_info(o, name) : nullptr;
  if (!f) return false;
  void *k = fn<void *(*)(void *)>(boot.meta.class_from_type)(fn<void *(*)(void *)>(boot.meta.field_get_type)(f));
  if (k && fn<bool (*)(void *)>(boot.meta.class_is_valuetype)(k)) { int v = 0; fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(o, f, &v); return v != 0; }
  void *v = nullptr; fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(o, f, &v);
  return v && (!inner || field_set(v, inner, nullptr));
}
std::string utf8(void *s) {
  if (!s) return {};
  int n = fn<int (*)(void *)>(dlsym(boot.handle, "il2cpp_string_length"))(s);
  const uint16_t *p = fn<const uint16_t *(*)(void *)>(boot.runtime.string_chars)(s);
  std::string out;
  // JNI's modified UTF-8: each UTF-16 unit (surrogates too) on its own, matching GetStringUTFChars.
  for (int i = 0; i < n && i < 256; i++) {
    uint32_t c = p[i];
    if (c > 0 && c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 63)); }
    else { out += char(0xe0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
  }
  return out;
}
void *type_object(void *k) {
  auto get = reinterpret_cast<void *(*)(void *)>(dlsym(boot.handle, "il2cpp_type_get_object")); require(get, "type_get_object");
  return get(fn<void *(*)(void *)>(boot.meta.class_get_type)(k));
}
Root resolve(void *contract) {
  auto h = [](unsigned id) { return const_cast<void *>(md->opt_handles[id]); };
  Root type(type_object(h(H_IL_MAP))); void *args[] = {type.get()};
  Root map(invoke(h(H_IL_FIND_M), nullptr, args)); if (!map.get()) return {};
  Root container(invoke(h(H_IL_CONTAINER_M), map.get())); if (!container.get()) return {};
  Root target(type_object(contract)); void *resolve_args[] = {target.get()};
  return Root(invoke(h(H_IL_RESOLVE_M), container.get(), resolve_args));
}
int list_count(void *list) { return unboxed<int>(call(list, "get_Count")); }
void *list_item(void *list, int i) { void *a[] = {&i}; return call(list, "get_Item", 1, a); }

constexpr int kMaxSpecies = 1025;  // National Dex size; GameMaster lookups stop here
struct Config {
  bool active = true, also_rare = false; int rare_mask = PGR_TAG_RARE_SHINY | PGR_TAG_RARE_IV, iv_min = 90, rare_berry = 706, candy_berry = 705;
  std::set<int> ultra; std::set<std::string> catch_tags, candy_tags;
};
std::mutex lock;
Config cfg;
bool dirty = true, ready = false, configured = false;
std::string last_config;
uint32_t revision = 0;
std::vector<int> family_of, class_of;  // owner thread only, filled once
std::vector<bool> tagged_catch, tagged_candy;  // by species; guarded by lock
std::string tag_names;                 // guarded by lock
uint64_t next_read = 0;

void build_species_table() {
  Root gm = resolve(cls("", "IGameMasterData"));
  require(gm.get() && unboxed<bool>(call(gm.get(), "get_IsReady")), "game master");
  // The Int32 overload of Get_PokemonSettings; HoloPokemonId takes the same int argument.
  void *get = nullptr, *it = nullptr;
  while (void *m = fn<void *(*)(void *, void **)>(boot.meta.class_get_methods)(klass(gm.get()), &it)) {
    if (strcmp(fn<const char *(*)(void *)>(boot.meta.method_get_name)(m), "Get_PokemonSettings") || fn<unsigned (*)(void *)>(boot.meta.method_get_param_count)(m) != 1) continue;
    char *t = fn<char *(*)(void *)>(boot.meta.type_get_name)(fn<void *(*)(void *, unsigned)>(boot.meta.method_get_param)(m, 0));
    bool match = t && (!strcmp(t, "System.Int32") || strstr(t, "HoloPokemonId"));
    if (t) fn<void (*)(void *)>(boot.meta.mem_free)(t);
    if (match) { get = m; break; }
  }
  require(get, "Get_PokemonSettings");
  std::vector<int> family(kMaxSpecies + 1), klass_ids(kMaxSpecies + 1);
  for (int id = 1; id <= kMaxSpecies; id++) {
    void *a[] = {&id};
    void *settings = nullptr;
    try { settings = invoke(get, gm.get(), a); } catch (const Failure &) { continue; }
    if (!settings) continue;
    family[id] = field_int(settings, "familyId_");
    klass_ids[id] = field_int(settings, "pokemonClass_");
  }
  family_of.swap(family); class_of.swap(klass_ids);
}

void read_tags() {
  if (family_of.empty()) build_species_table();
  Root service = resolve(cls("Niantic.Holoholo.Tagging", "ITaggingRpcService"));
  require(service.get() != nullptr, "tag service");
  Root wait(call(service.get(), "WaitForTagCacheInitialized"));
  void *done = wait.get() ? field_info(wait.get(), "completeCalled") : nullptr;
  bool complete = false;
  if (done) fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(wait.get(), done, &complete);
  if (!complete) return;  // tag cache still loading; try again on the next read
  std::set<std::string> want_catch, want_candy;
  { std::lock_guard<std::mutex> g(lock); want_catch = cfg.catch_tags; want_candy = cfg.candy_tags; }
  std::set<int> catch_families, candy_families;
  std::string names;
  int n = unboxed<int>(call(service.get(), "get_NumberOfTags"));
  require(n >= 0 && n <= 1000, "tag count");
  for (int i = 0; i < n; i++) {
    void *a[] = {&i};
    Root row(call(service.get(), "GetCachedTagToPokemonBindingForIndex", 1, a));
    if (!row.get()) continue;
    void *tag = field_ref(row.get(), "TagProto");
    if (!tag) continue;
    std::string name = utf8(field_ref(tag, "Name"));
    if (name.empty()) continue;
    names += (names.empty() ? "" : "\n") + name;
    const std::string key = pgr_evolution::normalize_tag(name);
    const bool for_catch = want_catch.count(key) > 0, for_candy = want_candy.count(key) > 0;
    if (!for_catch && !for_candy) continue;
    void *list = field_ref(row.get(), "PokemonProtos");
    int count = list ? list_count(list) : 0;
    for (int j = 0; j < count && j < 25000; j++) {
      void *p = list_item(list, j);
      int id = p ? field_int(p, "pokemonId_") : 0;
      if (id <= 0 || id > kMaxSpecies) continue;
      // A species without a family still counts through its own id (family -id).
      const int family = family_of[id] ? family_of[id] : -id;
      if (for_catch) catch_families.insert(family);
      if (for_candy) candy_families.insert(family);
    }
  }
  std::vector<bool> by_catch(kMaxSpecies + 1), by_candy(kMaxSpecies + 1);
  for (int id = 1; id <= kMaxSpecies; id++) {
    const int family = family_of[id] ? family_of[id] : -id;
    by_catch[id] = catch_families.count(family) > 0; by_candy[id] = candy_families.count(family) > 0;
  }
  std::lock_guard<std::mutex> g(lock);
  tagged_catch.swap(by_catch); tagged_candy.swap(by_candy); tag_names = names; ready = true;
}

bool rare_species(const Config &c, int species) {
  if ((c.rare_mask & PGR_TAG_RARE_CLASS) && species > 0 && species < int(class_of.size()) && class_of[species] >= 1 && class_of[species] <= 3) return true;
  return (c.rare_mask & PGR_TAG_RARE_SPECIES) && c.ultra.count(species);
}
bool in(const std::vector<bool> &v, int species) { return species > 0 && species < int(v.size()) && v[species]; }
// Rare criteria only known after an encounter (shiny, IVs, costume, background).
constexpr int kEncounterRare = PGR_TAG_RARE_SHINY | PGR_TAG_RARE_SHUNDO | PGR_TAG_RARE_HUNDO | PGR_TAG_RARE_NUNDO |
                               PGR_TAG_RARE_IV | PGR_TAG_RARE_COSTUME | PGR_TAG_RARE_CARD;

// ---- CalcyIV export ----
std::atomic<bool> calcy_requested{false};
std::string calcy_rows;  // guarded by lock
bool calcy_ready = false;
// An enum/int field that may be missing on this game version (reads 0).
int opt_int(void *o, const char *name) {
  void *f = o ? field_info(o, name) : nullptr;
  if (!f) return 0;
  int v = 0; fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(o, f, &v); return v;
}
float opt_float(void *o, const char *name) {
  void *f = o ? field_info(o, name) : nullptr;
  if (!f) return 0;
  float v = 0; fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(o, f, &v); return v;
}
// "THUNDER_SHOCK_FAST" -> "Thunder Shock", from the move enum's literal names.
std::map<int, std::string> move_names;
void load_move_names(void *proto) {
  if (!move_names.empty()) return;
  void *f = field_info(proto, "move1_"); require(f, "move1_");
  void *k = fn<void *(*)(void *)>(boot.meta.class_from_type)(fn<void *(*)(void *)>(boot.meta.field_get_type)(f));
  auto next = fn<void *(*)(void *, void **)>(dlsym(boot.handle, "il2cpp_class_get_fields"));
  auto name_of = fn<const char *(*)(void *)>(dlsym(boot.handle, "il2cpp_field_get_name"));
  void *it = nullptr;
  while (void *e = next(k, &it)) {
    if ((fn<int (*)(void *)>(boot.meta.field_get_flags)(e) & 0x8050) != 0x8050) continue;
    int v = 0; fn<void (*)(void *, void *)>(boot.runtime.field_static_get_value)(e, &v);
    std::string raw = name_of(e), out;
    if (raw.size() > 5 && raw.compare(raw.size() - 5, 5, "_FAST") == 0) raw.resize(raw.size() - 5);
    bool up = true;
    for (char c : raw) { if (c == '_') { out += ' '; up = true; } else { out += up ? c : char(c >= 'A' && c <= 'Z' ? c + 32 : c); up = false; } }
    move_names[v] = out;
  }
}
std::string move_name(int id) { auto it = move_names.find(id); return id && it != move_names.end() ? it->second : std::string(); }
double level_of(float cpm) {
  int best = 0;
  for (int i = 1; i < 99; i++) if (std::fabs(pgr_pvp_data::cpm[i] - cpm) < std::fabs(pgr_pvp_data::cpm[best] - cpm)) best = i;
  return 1 + best * 0.5;
}
std::string clean(std::string s) { for (char &c : s) if (c == '\t' || c == '\n' || c == '\n') c = ' '; return s; }
void build_calcy() {
  Root bag = resolve(cls("Niantic.Holoholo", "IPokemonBag"));
  require(bag.get() != nullptr, "Pokemon storage");
  Root all(call(bag.get(), "get_AllPokemon")); require(all.get() != nullptr, "Pokemon storage list");
  void *k = klass(all.get()), *pokemon_class = cls("Holoholo.Rpc", "PokemonProto");
  const int n = unboxed<int>(invoke(method(k, "get_Count", 0), all.get())); require(n >= 0 && n <= 30000, "Pokemon storage size");
  Root array(fn<void *(*)(void *, uintptr_t)>(boot.runtime.array_new)(pokemon_class, uintptr_t(n))); require(array.get() != nullptr, "storage copy");
  int zero = 0; void *args[] = {array.get(), &zero}; invoke(method(k, "CopyTo", 2), all.get(), args);
  std::string rows;
  for (int i = 0; i < n; i++) {
    void *p = *reinterpret_cast<void **>(static_cast<char *>(array.get()) + 0x20 + size_t(i) * 8);
    if (!p || klass(p) != pokemon_class || opt_int(p, "isEgg_")) continue;
    load_move_names(p);
    void *display = field_ref(p, "pokemonDisplay_");
    long long created = 0;
    if (void *f = field_info(p, "creationTimeMs_")) fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(p, f, &created);
    char head[256];
    snprintf(head, sizeof head, "%d\t%d\t%d\t%d\t%d\t%d\t%.1f\t", field_int(p, "pokemonId_"), field_int(p, "cp_"), field_int(p, "maxStamina_"),
             field_int(p, "individualAttack_"), field_int(p, "individualDefense_"), field_int(p, "individualStamina_"),
             level_of(opt_float(p, "cpMultiplier_") + opt_float(p, "additionalCpMultiplier_")));
    char tail[160];
    snprintf(tail, sizeof tail, "\t%d\t%d\t%d\t%d\t", opt_int(display, "gender_"), opt_int(display, "alignment_"), opt_int(p, "isLucky_"), opt_int(p, "favorite_"));
    char size[96];
    snprintf(size, sizeof size, "\t%.3f\t%.3f\t%lld\n", opt_float(p, "heightM_"), opt_float(p, "weightKg_"), created);
    rows += head + move_name(field_int(p, "move1_")) + "\t" + move_name(field_int(p, "move2_")) + "\t" + move_name(opt_int(p, "move3_")) +
            tail + clean(utf8(field_ref(p, "nickname_"))) + size;
  }
  std::lock_guard<std::mutex> g(lock);
  calcy_rows.swap(rows); calcy_ready = true;
}
} // namespace

void pgr_calcy_request() { { std::lock_guard<std::mutex> g(lock); calcy_ready = false; calcy_rows.clear(); } calcy_requested = true; }
int pgr_calcy_take(std::string *out) {
  std::lock_guard<std::mutex> g(lock);
  if (!calcy_ready) return -1;
  out->swap(calcy_rows); calcy_ready = false;
  return 0;
}

void *pgr_encounter_pokemon(void *wild) {
  if (!initialized || failed || !wild) return nullptr;
  // Same path as Shundo Sniper: the map Pokemon's EncounterOutProto field -> Pokemon (WildPokemonProto)
  // -> Pokemon (PokemonProto, with IVs). The map's own PokemonProto has no IVs.
  try {
    static void *klass_seen = nullptr, *field = nullptr;
    void *k = klass(wild);
    if (k != klass_seen) {
      field = nullptr; klass_seen = k;
      auto next = fn<void *(*)(void *, void **)>(dlsym(boot.handle, "il2cpp_class_get_fields"));
      void *it = nullptr;
      while (void *f = next(k, &it)) {
        if (fn<int (*)(void *)>(boot.meta.field_get_flags)(f) & 0x10) continue;
        char *t = fn<char *(*)(void *)>(boot.meta.type_get_name)(fn<void *(*)(void *)>(boot.meta.field_get_type)(f));
        bool match = t && !strcmp(t, "Holoholo.Rpc.EncounterOutProto");
        if (t) fn<void (*)(void *)>(boot.meta.mem_free)(t);
        if (match) { field = f; break; }
      }
    }
    if (!field) return nullptr;
    void *response = nullptr;
    fn<void (*)(void *, void *, void *)>(boot.runtime.field_get_value)(wild, field, &response);
    void *encounter = response ? field_ref(response, "Pokemon") : nullptr;
    void *pokemon = encounter ? field_ref(encounter, "Pokemon") : nullptr;
    return pokemon;
  } catch (const Failure &) { return nullptr; }
}

void pgr_tag_catch_config(const char *text) {
  Config next;
  std::vector<std::string> parts;
  std::string s = text ? text : "", part;
  std::stringstream in(s);
  while (std::getline(in, part, ';')) parts.push_back(part);
  if (parts.size() >= 8 && parts[0] == "2") {
    next.active = parts[1] == "1"; next.also_rare = parts[2] == "1";
    next.rare_mask = atoi(parts[3].c_str()); next.iv_min = atoi(parts[4].c_str());
    next.rare_berry = atoi(parts[5].c_str()); next.candy_berry = atoi(parts[6].c_str());
    std::stringstream ids(parts[7]);
    while (std::getline(ids, part, ',')) if (int id = atoi(part.c_str())) next.ultra.insert(id);
    for (int list = 0; list < 2; list++) {
      std::stringstream tags(parts.size() > size_t(8 + list) ? parts[8 + list] : std::string());
      while (std::getline(tags, part, '\t')) if (!part.empty()) (list ? next.candy_tags : next.catch_tags).insert(pgr_evolution::normalize_tag(part));
    }
  }
  std::lock_guard<std::mutex> g(lock);
  if (s != last_config) { last_config = s; ++revision; }
  if (next.catch_tags != cfg.catch_tags || next.candy_tags != cfg.candy_tags) { dirty = true; ready = false; }
  cfg = next; configured = true;
}

int pgr_tag_catch_tags(char *out, int cap) {
  std::lock_guard<std::mutex> g(lock);
  int n = int(tag_names.size()) < cap - 1 ? int(tag_names.size()) : cap - 1;
  if (n < 0) return 0;
  memcpy(out, tag_names.data(), n); out[n] = 0;
  return n;
}

uint32_t pgr_tag_catch_revision() { std::lock_guard<std::mutex> g(lock); return revision; }

int pgr_tag_catch_prefilter(int species) {
  std::lock_guard<std::mutex> g(lock);
  if (!configured) return 0;
  const bool tags = cfg.active && !cfg.catch_tags.empty();
  if (tags && !ready) return 0;
  if (species <= 0) return tags ? 2 : 1;
  if (tags && in(tagged_catch, species)) return 1;
  return cfg.also_rare && ((cfg.rare_mask & kEncounterRare) || rare_species(cfg, species)) ? 1 : 0;
}

int pgr_tag_rules(void *pokemon, int species, bool shiny, int atk, int def, int sta, PgrTagRules *out) {
  *out = PgrTagRules{};
  Config c;
  {
    std::lock_guard<std::mutex> g(lock);
    const bool tags = cfg.active && (!cfg.catch_tags.empty() || !cfg.candy_tags.empty());
    if (!configured || (tags && !ready)) return -1;
    c = cfg;
    out->filter = c.active && !c.catch_tags.empty();
    out->tagged_catch = out->filter && in(tagged_catch, species);
    out->candy = c.active && in(tagged_candy, species);
  }
  // shortcut: 0/0/0 is treated as "IVs not sent", so a real nundo is missed.
  const bool iv_valid = atk >= 0 && atk <= 15 && def >= 0 && def <= 15 && sta >= 0 && sta <= 15 && atk + def + sta > 0;
  const int iv = atk + def + sta, m = c.rare_mask;
  bool rare = rare_species(c, species) ||
              ((m & PGR_TAG_RARE_SHINY) && shiny) ||
              ((m & PGR_TAG_RARE_SHUNDO) && shiny && iv_valid && iv == 45) ||
              ((m & PGR_TAG_RARE_HUNDO) && iv_valid && iv == 45) ||
              ((m & PGR_TAG_RARE_NUNDO) && iv_valid && iv == 0) ||
              ((m & PGR_TAG_RARE_IV) && iv_valid && iv * 100 >= c.iv_min * 45);
  if (!rare && pokemon && (m & (PGR_TAG_RARE_COSTUME | PGR_TAG_RARE_CARD)) && initialized && !failed) {
    try {
      void *display = field_ref(pokemon, "pokemonDisplay_");
      rare = ((m & PGR_TAG_RARE_COSTUME) && field_set(display, "costume_", nullptr)) ||
             ((m & PGR_TAG_RARE_CARD) && field_set(display, "locationCard_", "locationCard_"));
    } catch (const Failure &) {}
  }
  out->rare = rare; out->also_rare = c.also_rare; out->rare_berry = c.rare_berry; out->candy_berry = c.candy_berry;
  return 0;
}

void pgr_tag_catch_tick(uint64_t tid, const PgrSharedMetadataOutput *metadata) {
  if (failed || !tid || tid != uint64_t(gettid()) || (owner && owner != tid)) return;
  // Bootstrap even while tag catch is off: Smart Catch reads encounter IVs through pgr_encounter_pokemon.
  if (!initialized) {
    try {
      auto api = static_cast<const PgrBootApi *>(pgrazor_runtime_bootstrap_api(1));
      boot.version = 1; boot.size = sizeof(boot);
      require(api && api->snapshot(&boot) == 0 && metadata, "runtime bootstrap");
      md = metadata; owner = tid; initialized = true;
    } catch (...) { failed = true; return; }
  }
  if (calcy_requested.exchange(false)) {
    try { build_calcy(); }
    catch (const Failure &e) { __android_log_print(ANDROID_LOG_WARN, "PGRazorCalcy", "%s", e.where); std::lock_guard<std::mutex> g(lock); calcy_rows = std::string("ERROR\t") + e.where; calcy_ready = true; }
  }
  bool reread;
  {
    std::lock_guard<std::mutex> g(lock);
    // shortcut: re-reads the tags every 60 s instead of on each catch/transfer; hook the tag cache callback if that lag matters.
    reread = configured && (dirty || clock_ms() >= next_read);
    if (reread) { dirty = false; next_read = clock_ms() + 60000; }
  }
  if (!reread) return;
  try {
    read_tags();
  } catch (const Failure &e) {
    __android_log_print(ANDROID_LOG_WARN, "PGRazorTagCatch", "%s", e.where);
    std::lock_guard<std::mutex> g(lock);
    next_read = clock_ms() + 10000;
  } catch (...) {
    failed = true;
  }
}
