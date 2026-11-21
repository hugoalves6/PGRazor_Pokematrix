#pragma once
#include <cstdint>
#include <string>
struct PgrSharedMetadataOutput;
// VGP+ tag rules: candy families of the selected Pokemon storage tags for "Catch only" and "Candy species",
// plus the shared "rare" criteria. Config from Java (TagCatchSettings.config):
//   "2;active;alsoRare;rareMask;ivMin;rareBerry;candyBerry;<ultra-rare species csv>;<catch tags>;<candy tags>"
// with tag names joined by '\t' (';' and '\t' are stripped from names in Java).
enum { PGR_TAG_RARE_CLASS = 1, PGR_TAG_RARE_SHINY = 2, PGR_TAG_RARE_SHUNDO = 4, PGR_TAG_RARE_HUNDO = 8,
       PGR_TAG_RARE_NUNDO = 16, PGR_TAG_RARE_IV = 32, PGR_TAG_RARE_COSTUME = 64, PGR_TAG_RARE_CARD = 128,
       PGR_TAG_RARE_SPECIES = 256 };
struct PgrTagRules {
  bool filter;        // "Catch only" tags are in use
  bool tagged_catch;  // species is in a "Catch only" tag family
  bool candy;         // species is in a "Candy species" tag family
  bool rare, also_rare;
  int rare_berry, candy_berry;
};
void pgr_tag_catch_tick(uint64_t tid, const PgrSharedMetadataOutput *metadata);
void pgr_tag_catch_config(const char *text);
// The encountered PokemonProto (with IVs) from the map Pokemon's EncounterOutProto, or null. Owner thread.
void *pgr_encounter_pokemon(void *wild);
// "\n"-joined names of the player's own tags, as last read (empty until the first read).
int pgr_tag_catch_tags(char *out, int cap);
// Before an encounter (owner thread). species <= 0 asks the state: 0 not ready (skip everything), 1 no
// tag filter, 2 tag filter active. species > 0: 1 when tags or "also rare" want it encountered, else 0.
int pgr_tag_catch_prefilter(int species);
// Bumped whenever the settings change, so the VGP can forget Pokemon it skipped under the old rules.
uint32_t pgr_tag_catch_revision();
// CalcyIV export: request a storage snapshot (the owner thread builds it on the next tick), then poll.
// One row per line, tab-separated: species cp hp atk def sta level fast charged charged2
// gender alignment lucky favorite nickname heightM weightKg creationMs. Returns -1 while pending.
void pgr_calcy_request();
int pgr_calcy_take(std::string *out);
// After an encounter (owner thread). -1 until the settings arrive or while the tags load (skip), else 0.
int pgr_tag_rules(void *pokemon, int species, bool shiny, int atk, int def, int sta, PgrTagRules *out);
