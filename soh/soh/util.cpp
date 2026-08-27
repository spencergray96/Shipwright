#include "util.h"

#include <string.h>
#include <cctype>
#include <vector>
#include <algorithm>
#include <array>
#include <assert.h>
#include <spdlog/spdlog.h>
#include "Enhancements/randomizer/randomizerTypes.h"
#include <variables.h>

std::string invalidString = "";

std::vector<std::string> sceneNames = {
    "Inside the Deku Tree",
    "Dodongo's Cavern",
    "Inside Jabu-Jabu's Belly",
    "Forest Temple",
    "Fire Temple",
    "Water Temple",
    "Spirit Temple",
    "Shadow Temple",
    "Bottom of the Well",
    "Ice Cavern",
    "Ganon's Tower",
    "Gerudo Training Ground",
    "Thieves' Hideout",
    "Inside Ganon's Castle",
    "Ganon's Tower (Collapsing)",
    "Inside Ganon's Castle (Collapsing)",
    "Treasure Box Shop",
    "Gohma's Lair",
    "King Dodongo's Lair",
    "Barinade's Lair",
    "Phantom Ganon's Lair",
    "Volvagia's Lair",
    "Morpha's Lair",
    "Twinrova's Lair",
    "Bongo Bongo's Lair",
    "Ganondorf's Lair",
    "Tower Collapse Exterior",
    "Market Entrance (Child - Day)",
    "Market Entrance (Child - Night)",
    "Market Entrance (Ruins)",
    "Back Alley (Child - Day)",
    "Back Alley (Child - Night)",
    "Market (Child - Day)",
    "Market (Child - Night)",
    "Market (Ruins)",
    "Temple of Time Exterior (Child - Day)",
    "Temple of Time Exterior (Child - Night)",
    "Temple of Time Exterior (Ruins)",
    "Know-It-All Brothers' House",
    "House of Twins",
    "Mido's House",
    "Saria's House",
    "Carpenter Boss's House",
    "Back Alley House (Man in Green)",
    "Bazaar",
    "Kokiri Shop",
    "Goron Shop",
    "Zora Shop",
    "Kakariko Potion Shop",
    "Market Potion Shop",
    "Bombchu Shop",
    "Happy Mask Shop",
    "Link's House",
    "Back Alley House (Dog Lady)",
    "Stable",
    "Impa's House",
    "Lakeside Laboratory",
    "Carpenters' Tent",
    "Gravekeeper's Hut",
    "Great Fairy's Fountain (Upgrades)",
    "Fairy's Fountain",
    "Great Fairy's Fountain (Spells)",
    "Grottos",
    "Grave (Redead)",
    "Grave (Fairy's Fountain)",
    "Royal Family's Tomb",
    "Shooting Gallery",
    "Temple of Time",
    "Chamber of the Sages",
    "Castle Hedge Maze (Day)",
    "Castle Hedge Maze (Night)",
    "Cutscene Map",
    "Dampe's Grave & Windmill",
    "Fishing Pond",
    "Castle Courtyard",
    "Bombchu Bowling Alley",
    "Ranch House & Silo",
    "Guard House",
    "Granny's Potion Shop",
    "Ganon's Tower Collapse & Arena",
    "House of Skulltula",
    "Hyrule Field",
    "Kakariko Village",
    "Graveyard",
    "Zora's River",
    "Kokiri Forest",
    "Sacred Forest Meadow",
    "Lake Hylia",
    "Zora's Domain",
    "Zora's Fountain",
    "Gerudo Valley",
    "Lost Woods",
    "Desert Colossus",
    "Gerudo's Fortress",
    "Haunted Wasteland",
    "Hyrule Castle",
    "Death Mountain Trail",
    "Death Mountain Crater",
    "Goron City",
    "Lon Lon Ranch",
    "Ganon's Castle Exterior",
    "Jungle Gym",
    "Ganondorf Test Room",
    "Depth Test",
    "Stalfos Mini-Boss Room",
    "Stalfos Boss Room",
    "Sutaru",
    "Castle Hedge Maze (Early)",
    "Sasa Test",
    "Treasure Chest Room",
    "Test Level", // 0x6E, this fork's first custom scene - was vanilla's unused slot
};

std::vector<std::string> itemNamesEng = {
    "Deku Stick",
    "Deku Nut",
    "Bomb",
    "Fairy Bow",
    "Fire Arrow",
    "Din's Fire",
    "Fairy Slingshot",
    "Fairy Ocarina",
    "Ocarina of Time",
    "Bombchu",
    "Hookshot",
    "Longshot",
    "Ice Arrow",
    "Farore's Wind",
    "Boomerang",
    "Lens of Truth",
    "Magic Bean",
    "Megaton Hammer",
    "Light Arrow",
    "Nayru's Love",
    "Empty Bottle",
    "Red Potion",
    "Green Potion",
    "Blue Potion",
    "Bottled Fairy",
    "Fish",
    "Lon Lon Milk & Bottle",
    "Ruto's Letter",
    "Blue Fire",
    "Bug",
    "Big Poe",
    "Lon Lon Milk (Half)",
    "Poe",
    "Weird Egg",
    "Chicken",
    "Zelda's Letter",
    "Keaton Mask",
    "Skull Mask",
    "Spooky Mask",
    "Bunny Hood",
    "Goron Mask",
    "Zora Mask",
    "Gerudo Mask",
    "Mask of Truth",
    "SOLD OUT",
    "Pocket Egg",
    "Pocket Cucco",
    "Cojiro",
    "Odd Mushroom",
    "Odd Potion",
    "Poacher's Saw",
    "Goron's Sword (Broken)",
    "Prescription",
    "Eyeball Frog",
    "Eye Drops",
    "Claim Check",
    "Fairy Bow & Fire Arrow",
    "Fairy Bow & Ice Arrow",
    "Fairy Bow & Light Arrow",
    "Kokiri Sword",
    "Master Sword",
    "Giant's Knife & Biggoron's Sword",
    "Deku Shield",
    "Hylian Shield",
    "Mirror Shield",
    "Kokiri Tunic",
    "Goron Tunic",
    "Zora Tunic",
    "Kokiri Boots",
    "Iron Boots",
    "Hover Boots",
    "Bullet Bag (30)",
    "Bullet Bag (40)",
    "Bullet Bag (50)",
    "Quiver (30)",
    "Big Quiver (40)",
    "Biggest Quiver (50)",
    "Bomb Bag (20)",
    "Big Bomb Bag (30)",
    "Biggest Bomb Bag (40)",
    "Goron's Bracelet",
    "Silver Gauntlets",
    "Golden Gauntlets",
    "Silver Scale",
    "Golden Scale",
    "Giant's Knife (Broken)",
    "Adult's Wallet",
    "Giant's Wallet",
    "Deku Seeds (5)",
    "Fishing Pole",
    "Minuet of Forest",
    "Bolero of Fire",
    "Serenade of Water",
    "Requiem of Spirit",
    "Nocturne of Shadow",
    "Prelude of Light",
    "Zelda's Lullaby",
    "Epona's Song",
    "Saria's Song",
    "Sun's Song",
    "Song of Time",
    "Song of Storms",
    "Forest Medallion",
    "Fire Medallion",
    "Water Medallion",
    "Spirit Medallion",
    "Shadow Medallion",
    "Light Medallion",
    "Kokiri's Emerald",
    "Goron's Ruby",
    "Zora's Sapphire",
    "Stone of Agony",
    "Gerudo's Card",
    "Gold Skulltula Token",
    "Heart Container",
    "Piece of Heart",
    "Boss Key",
    "Compass",
    "Dungeon Map",
    "Small Key",
    "Small Magic Jar",
    "Large Magic Jar",
    "Piece of Heart",
    "[Removed]",
    "[Removed]",
    "[Removed]",
    "[Removed]",
    "[Removed]",
    "[Removed]",
    "[Removed]",
    "Lon Lon Milk",
    "Recovery Heart",
    "Green Rupee",
    "Blue Rupee",
    "Red Rupee",
    "Purple Rupee",
    "Huge Rupee",
    "[Removed]",
    "Deku Sticks (5)",
    "Deku Sticks (10)",
    "Deku Nuts (5)",
    "Deku Nuts (10)",
    "Bombs (5)",
    "Bombs (10)",
    "Bombs (20)",
    "Bombs (30)",
    "Arrows (Small)",
    "Arrows (Medium)",
    "Arrows (Large)",
    "Deku Seeds (30)",
    "Bombchu (5)",
    "Bombchu (20)",
    "Deku Stick Upgrade (20)",
    "Deku Stick Upgrade (30)",
    "Deku Nut Upgrade (30)",
    "Deku Nut Upgrade (40)",
    "[Removed]", // ITEM_CUSTOM
    "Roc's Feather",
};

std::vector<std::string> itemNamesFra = {
    "Bâton Mojo",
    "Noix Mojo",
    "Bombe",
    "Arc des Fées",
    "Flèche de Feu",
    "Feu de Din",
    "Lance-Pierre des Fées",
    "Ocarina des Fées",
    "Ocarina du Temps",
    "Missile Teigneux",
    "Grappin",
    "Super Grappin",
    "Flèche de Glace",
    "Vent de Farore",
    "Boomerang",
    "Monocle de Vérité",
    "Haricot Magique",
    "Masse des Titans",
    "Flèche de Lumière",
    "Amour de Nayru",
    "Bouteille Vide",
    "Potion Rouge",
    "Potion Verte",
    "Potion Bleue",
    "Fée en Bouteille",
    "Poisson",
    "Lait Lon Lon et Bouteille",
    "Lettre de Ruto",
    "Flamme Bleue",
    "Insectes",
    "Grand Spectre",
    "Lait Lon Lon (Demi)",
    "Spectre",
    "Oeuf Suspect",
    "Poule",
    "Lettre de Zelda",
    "Masque Renard",
    "Masque de Mort",
    "Masque du Fantôme",
    "Capuche de Lapin",
    "Masque Goron",
    "Masque Zora",
    "Masque Gerudo",
    "Masque de Vérité",
    "ÉPUISÉ",
    "Oeuf de Poche",
    "Cocotte de Poche",
    "Cojiro",
    "Champignon Suspect",
    "Potion Suspecte",
    "Scie du Chasseur",
    "Épée Goron (Cassée)",
    "Ordonnance",
    "Crapaud-qui-louche",
    "Super Gouttes",
    "Certificat",
    "Arc des Fées & Flèche de Feu",
    "Arc des Fées & Flèche de Glace",
    "Arc des Fées & Flèche de Lumière",
    "Épée Kokiri",
    "Épée de Légende",
    "Lame des Géants & Épée Biggoron",
    "Bouclier Mojo",
    "Bouclier Hylien",
    "Bouclier Miroir",
    "Tunique Kokiri",
    "Tunique Goron",
    "Tunique Zora",
    "Bottes Kokiri",
    "Bottes de Plomb",
    "Bottes des Airs",
    "Sac de Graines (30)",
    "Sac de Graines (40)",
    "Sac de Graines (50)",
    "Carquois (30)",
    "Grand Carquois (40)",
    "Énorme Grand Carquois (50)",
    "Sac de Bombes (20)",
    "Gros Sac de Bombes (30)",
    "Énorme Sac de Bombes (40)",
    "Bracelet Goron",
    "Gantelets d'Argent",
    "Gantelets d'Or",
    "Écaille d'Argent",
    "Écaille d'Or",
    "Lame des Géants (Cassée)",
    "Grande Bourse",
    "Bourse de Géant",
    "Graines Mojo (5)",
    "Canne à Pêche",
    "Menuet des Bois",
    "Boléro du Feu",
    "Sérénade de l'Eau",
    "Requiem de l'Esprit",
    "Nocturne de l'Ombre",
    "Prélude de la Lumière",
    "Berceuse de Zelda",
    "Chant d'Epona",
    "Chant de Saria",
    "Chant du Soleil",
    "Chant du Temps",
    "Chant des Tempêtes",
    "Médaillon de la Forêt",
    "Médaillon du Feu",
    "Médaillon de l'Eau",
    "Médaillon de l'Esprit",
    "Médaillon de l'Ombre",
    "Médaillon de la Lumière",
    "Émeraude Kokiri",
    "Rubis Goron",
    "Saphir Zora",
    "Pierre de Souffrance",
    "Carte Gerudo",
    "Symbole de Skulltula d'Or",
    "Réceptacle de Coeur",
    "Quart de Coeur",
    "Clé du Boss",
    "Boussole",
    "Carte du Donjon",
    "Petite Clé",
    "Petite Magie",
    "Grande Magie",
    "Quart de Coeur",
    "[Retiré]",
    "[Retiré]",
    "[Retiré]",
    "[Retiré]",
    "[Retiré]",
    "[Retiré]",
    "[Retiré]",
    "Lait Lon Lon",
    "Coeur",
    "Rubis Vert",
    "Rubis Bleu",
    "Rubis Rouge",
    "Rubis Pourpre",
    "Énorme Rubis",
    "[Retiré]",
    "Bâtons Mojo (5)",
    "Bâtons Mojo (10)",
    "Noix Mojo (5)",
    "Noix Mojo (10)",
    "Bombes (5)",
    "Bombes (10)",
    "Bombes (20)",
    "Bombes (30)",
    "Flèches (Petites)",
    "Flèches (Moyennes)",
    "Flèches (Grandes)",
    "Graines Mojo (30)",
    "Missile Teigneux (5)",
    "Missile Teigneux (20)",
    "Amélioration des Bâtons Mojo (20)",
    "Amélioration des Bâtons Mojo (30)",
    "Amélioration des Noix Mojo (30)",
    "Amélioration des Noix Mojo (40)",
    "[Retiré]", // ITEM_CUSTOM
    "Plume de Roc",
};

std::vector<std::string> itemNamesGer = {
    "Deku-Stab",
    "Deku-Nuß",
    "Bombe",
    "Feen-Bogen",
    "Feuer-Pfeil",
    "Dins Feuerinferno",
    "Feen-Schleuder",
    "Feen-Okarina",
    "Okarina der Zeit",
    "Krabbelmine",
    "Fanghaken",
    "Enterhaken",
    "Eis-Pfeil",
    "Farores Donnersturm",
    "Bumerang",
    "Auge der Wahrheit",
    "Wundererbse",
    "Stahlhammer",
    "Licht-Pfeil",
    "Nayrus Umarmung",
    "Leere Flasche",
    "Rotes Elixier",
    "Grünes Elixier",
    "Blaues Elixier",
    "Flasche (Fee)",
    "Fisch",
    "Flasche (Milch)",
    "Rutos Brief",
    "Blaues Feuer",
    "Käfer",
    "Nachtschwärmer",
    "Lon Lon-Milch (Halbe Füllung)",
    "Irrlicht",
    "Seltsames Ei",
    "Huhn",
    "Zeldas Brief",
    "Fuchs-Maske",
    "Geister-Maske",
    "Schädel-Maske",
    "Hasenohren",
    "Goronen-Maske",
    "Zora-Maske",
    "Gerudo-Maske",
    "Maske des Wissens",
    "AUSVERKAUFT",
    "Ei",
    "Kiki",
    "Henni",
    "Schimmelpilz",
    "Modertrank",
    "Säge",
    "Zerbr. Goronen-Schwert",
    "Rezept",
    "Glotzfrosch",
    "Augentropfen",
    "Zertifikat",
    "Feen-Bogen & Feuer-Pfeil",
    "Feen-Bogen & Eis-Pfeil",
    "Feen-Bogen & Licht-Pfeil",
    "Kokiri-Schwert",
    "Master-Schwert",
    "Langschwert & Biggoron-Schwert",
    "Deku-Schild",
    "Hylia-Schild",
    "Spiegel-Schild",
    "Kokiri-Rüstung",
    "Goronen-Rüstung",
    "Zora-Rüstung",
    "Lederstiefel",
    "Eisenstiefel",
    "Gleitstiefel",
    "Munitionstasche (30)",
    "Große Munitionstasche (40)",
    "Riesen-Munitionstasche (50)",
    "Köcher (30)",
    "Großer Köcher (40)",
    "Riesenköcher (50)",
    "Bombentasche (20)",
    "Große Bombentasche (30)",
    "Riesen-Bombentasche (40)",
    "Goronen-Armband",
    "Krafthandschuhe",
    "Titanhandschuhe",
    "Silberne Schuppe",
    "Goldene Schuppe",
    "Zerbr. Langschwert",
    "Große Börse",
    "Riesenbörse",
    "Deku-Kerne (5)",
    "Angelrute",
    "Menuett des Waldes",
    "Bolero des Feuers",
    "Serenade des Wassers",
    "Requiem der Geister",
    "Nocturne des Schattens",
    "Kantate des Lichts",
    "Zeldas Wiegenlied",
    "Eponas Lied",
    "Salias Lied",
    "Hymne der Sonne",
    "Hymne der Zeit",
    "Hymne des Sturms",
    "Amulett des Waldes",
    "Amulett des Feuers",
    "Amulett des Wassers",
    "Amulett der Geister",
    "Amulett des Schattens",
    "Amulett des Lichts",
    "Kokiri-Smaragd",
    "Goronen-Rubin",
    "Zora-Saphir",
    "Stein des Wissens",
    "Gerudo-Paß",
    "Skulltula-Symbol",
    "Herzcontainer",
    "Herzteil",
    "Master-Schlüssel",
    "Kompaß",
    "Labyrinth-Karte",
    "Kleiner Schlüssel",
    "Kleine Magieflasche",
    "Große Magieflasche",
    "Herzteil",
    "[Entfernt]",
    "[Entfernt]",
    "[Entfernt]",
    "[Entfernt]",
    "[Entfernt]",
    "[Entfernt]",
    "[Entfernt]",
    "Lon Lon-Milch",
    "Herz",
    "Grüner Rubin",
    "Blauer Rubin",
    "Roter Rubin",
    "Violetter Rubin",
    "Silberner Rubin",
    "[Entfernt]",
    "Deku-Stäbe (5)",
    "Deku-Stäbe (10)",
    "Deku-Nüsse (5)",
    "Deku-Nüsse (10)",
    "Bomben (5)",
    "Bomben (10)",
    "Bomben (20)",
    "Bomben (30)",
    "Pfeile (5)",
    "Pfeile (10)",
    "Pfeile (30)",
    "Deku-Kerne (30)",
    "Krabbelminen (5)",
    "Krabbelminen (20)",
    "Deku-Stab-Kapazität (20)",
    "Deku-Stab-Kapazität (30)",
    "Deku-Nuß-Kapazität (30)",
    "Deku-Nuß-Kapazität (40)",
    "[Entfernt]", // ITEM_CUSTOM
    "Greifenfeder",
};

std::vector<std::string> questItemNamesEng = {
    "Forest Medallion",   "Fire Medallion",   "Water Medallion", "Spirit Medallion",     "Shadow Medallion",
    "Light Medallion",    "Minuet of Forest", "Bolero of Fire",  "Serenade of Water",    "Requiem of Spirit",
    "Nocturne of Shadow", "Prelude of Light", "Zelda's Lullaby", "Epona's Song",         "Saria's Song",
    "Sun's Song",         "Song of Time",     "Song of Storms",  "Kokiri's Emerald",     "Goron's Ruby",
    "Zora's Sapphire",    "Stone of Agony",   "Gerudo's Card",   "Gold Skulltula Token",
};

std::vector<std::string> questItemNamesFra = {
    "Médaillon de la Forêt", "Médaillon du Feu",        "Médaillon de l'Eau",  "Médaillon de l'Esprit",
    "Médaillon de l'Ombre",  "Médaillon de la Lumière", "Menuet des Bois",     "Boléro du Feu",
    "Sérénade de l'Eau",     "Requiem de l'Esprit",     "Nocturne de l'Ombre", "Prélude de la Lumière",
    "Berceuse de Zelda",     "Chant d'Epona",           "Chant de Saria",      "Chant du Soleil",
    "Chant du Temps",        "Chant des Tempêtes",      "Émeraude Kokiri",     "Rubis Goron",
    "Saphir Zora",           "Pierre de Souffrance",    "Carte Gerudo",        "Symbole de Skulltula d'Or",
};

std::vector<std::string> questItemNamesGer = {
    "Amulett des Waldes",
    "Amulett des Feuers",
    "Amulett des Wassers",
    "Amulett der Geister",
    "Amulett des Schattens",
    "Amulett des Lichts",
    "Menuett des Waldes",
    "Bolero des Feuers",
    "Serenade des Wassers",
    "Requiem der Geister",
    "Nocturne des Schattens",
    "Kantate des Lichts",
    "Zeldas Wiegenlied",
    "Eponas Lied",
    "Salias Lied",
    "Hymne der Sonne",
    "Hymne der Zeit",
    "Hymne des Sturms",
    "Kokiri-Smaragd",
    "Goronen-Rubin",
    "Zora-Saphir",
    "Stein des Wissens",
    "Gerudo-Paß",
    "Goldenes Skulltula-Symbol",
};

std::array<std::string, RA_MAX> rcareaPrefixes = {
    "KF",
    "LW",
    "SFM",
    "HF",
    "LH",
    "GV",
    "GF",
    "Wasteland",
    "Colossus",
    "Market",
    "HC",
    "Kak",
    "Graveyard",
    "DMT",
    "GC",
    "DMC",
    "ZR",
    "ZD",
    "ZF",
    "LLR",
    "Deku Tree",
    "Dodongos Cavern",
    "Jabu Jabus Belly",
    "Forest Temple",
    "Fire Temple",
    "Water Temple",
    "Spirit Temple",
    "Shadow Temple",
    "Bottom of the Well",
    "Ice Cavern",
    "Gerudo Training Ground",
    "Ganon's Castle",
};

// Every scene the table defines, named from its own SCENE_* enum. Unlike the hand-written
// `sceneNames` at the top of this file, it is generated from tables/scene_table.h - so it is
// exactly SCENE_ID_MAX long and cannot fall out of step with the scene list however many scenes get
// appended. `sceneNames` stops at vanilla, so every custom scene lands past its end, which is what
// sturdy-bassoon#31 is about.
//
// The generation is an "X macro", a C idiom with no real equivalent in JS/TS. `scene_table.h` is
// not a header of declarations - it is 129 bare `DEFINE_SCENE(...)` calls with no definition of its
// own. Whoever includes it defines that macro first and so decides what the file expands into: the
// SceneID enum defines it to emit `enum,`, and here it is defined to emit `#enumName,` - the `#`
// being the preprocessor's stringify operator, turning the token SCENE_DEKU_TREE into the string
// "SCENE_DEKU_TREE". So the same one list of scenes generates the enum, the scene table and this,
// and they cannot disagree. `#undef` after, because the definition would otherwise leak into
// whatever includes this file next.
#define DEFINE_SCENE(_0, _1, enumName, _3, _4, _5) #enumName,
static const char* const sSceneEnumNames[] = {
#include "tables/scene_table.h"
};
#undef DEFINE_SCENE

// CrashHandlerExt.cpp builds the same table from the same macro rather than sharing this one, and
// should keep doing so: it runs after the game has already crashed, where calling into a lazily
// built std::vector - one allocation on first use, possibly of the memory that just went wrong -
// is exactly what a crash handler must not do. Its copy is raw pointers and no allocation.

// The whole point of generating it is that it tracks the scene table, so say so to the compiler.
// Too many initialisers would already fail to build; too few would silently leave scenes unnamed,
// which is the direction that actually bites.
static_assert(std::size(sSceneEnumNames) == SCENE_ID_MAX,
              "sSceneEnumNames must cover exactly the scenes in tables/scene_table.h");

// "SCENE_LUMBRIDGE_CASTLE" -> "Lumbridge Castle". Not as good as a hand-written name, which is why
// it is only the fallback, but it beats "invalid" and it is never stale. Built once, on first use.
// Returning a reference to a function-local static is what lets callers hold `.c_str()` - see
// GetSceneName's contract below; both it and `invalidString` outlive any caller.
static const std::vector<std::string>& GeneratedSceneNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        out.reserve(std::size(sSceneEnumNames));
        for (const char* raw : sSceneEnumNames) {
            std::string name = raw;
            if (name.rfind("SCENE_", 0) == 0) {
                name = name.substr(strlen("SCENE_"));
            }
            std::string pretty;
            bool startOfWord = true;
            for (char c : name) {
                if (c == '_') {
                    pretty += ' ';
                    startOfWord = true;
                    continue;
                }
                // A letter straight after a digit keeps its case, so SCENE_TERRAIN_F2P_GREYBOX
                // comes out "Terrain F2P Greybox" rather than "F2p". Slug-shaped names like that
                // are the norm for this fork's scenes, which are the only ones that reach here.
                const bool afterDigit = !pretty.empty() && std::isdigit(static_cast<unsigned char>(pretty.back()));
                pretty += (startOfWord || afterDigit)
                              ? c
                              : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                startOfWord = false;
            }
            out.push_back(pretty);
        }
        return out;
    }();
    return names;
}

const std::string& SohUtils::GetSceneName(int32_t scene) {
    // Out of range of the scene table itself is the only genuinely invalid case, and it is the only
    // one that warns. It used to be anything past `sceneNames`, which meant every custom scene -
    // enough to pop an assert dialog just for opening the save editor's scene dropdown, since that
    // walks 0..SCENE_ID_MAX.
    if (scene < 0 || static_cast<size_t>(scene) >= std::size(sSceneEnumNames)) {
        SPDLOG_WARN("Passed invalid scene id to SohUtils::GetSceneName: ({})", scene);
        assert(false);
        return invalidString;
    }

    // The hand-written name where there is one, the generated one otherwise.
    if (static_cast<size_t>(scene) < sceneNames.size()) {
        return sceneNames[scene];
    }
    return GeneratedSceneNames()[scene];
}

const std::string& SohUtils::GetItemName(int32_t item) {
    const std::vector<std::string>* currentItemNames = nullptr;

    switch (gSaveContext.language) {
        case LANGUAGE_FRA:
            currentItemNames = &itemNamesFra;
            break;
        case LANGUAGE_GER:
            currentItemNames = &itemNamesGer;
            break;
        case LANGUAGE_ENG:
        default:
            currentItemNames = &itemNamesEng;
            break;
    }

    if (item < 0 || static_cast<size_t>(item) >= currentItemNames->size()) {
        SPDLOG_WARN("Passed invalid item id to SohUtils::GetItemName: ({})", item);
        assert(false);
        return invalidString;
    }

    return (*currentItemNames)[item];
}

const std::string& SohUtils::GetQuestItemName(int32_t item) {
    const std::vector<std::string>* currentQuestItemNames = nullptr;

    switch (gSaveContext.language) {
        case LANGUAGE_FRA:
            currentQuestItemNames = &questItemNamesFra;
            break;
        case LANGUAGE_GER:
            currentQuestItemNames = &questItemNamesGer;
            break;
        case LANGUAGE_ENG:
        default:
            currentQuestItemNames = &questItemNamesEng;
            break;
    }
    if (item < 0 || static_cast<size_t>(item) >= questItemNamesEng.size()) {
        SPDLOG_WARN("Passed invalid quest item id to SohUtils::GetQuestItemName: ({})", item);
        assert(false);
        return invalidString;
    }

    return (*currentQuestItemNames)[item];
}

const std::string& SohUtils::GetRandomizerCheckAreaPrefix(int32_t rcarea) {
    if (rcarea < 0 || static_cast<size_t>(rcarea) >= rcareaPrefixes.size()) {
        SPDLOG_WARN("Passed invalid rcarea to SohUtils::GetRandomizerCheckAreaPrefix: ({})", rcarea);
        assert(false);
        return invalidString;
    }

    return rcareaPrefixes[rcarea];
}

void SohUtils::CopyStringToCharArray(char* destination, std::string source, size_t size) {
    if (size > 0) {
        strncpy(destination, source.c_str(), size - 1);
        destination[size - 1] = '\0';
    }
}

std::string SohUtils::Sanitize(std::string stringValue) {
    stringValue.erase(std::remove_if(stringValue.begin(), stringValue.end(),
                                     [](char const c) { return '\n' == c || '\r' == c || '\0' == c || '\x1A' == c; }),
                      stringValue.end());

    return stringValue;
}

size_t SohUtils::CopyStringToCharBuffer(char* buffer, const std::string& source, const size_t maxBufferSize) {
    if (!source.empty() && maxBufferSize > 0) {
        memset(buffer, 0, maxBufferSize);
        const size_t copiedCharLen = std::min<size_t>(maxBufferSize - 1, source.length());
        memcpy(buffer, source.c_str(), copiedCharLen);
        return copiedCharLen;
    }

    return 0;
}

bool SohUtils::IsStringEmpty(std::string str) {
    // Remove spaces at the beginning of the string
    std::string::size_type start = str.find_first_not_of(' ');
    // Remove spaces at the end of the string
    std::string::size_type end = str.find_last_not_of(' ');

    // Check if the string is empty after stripping spaces
    return start == std::string::npos || end == std::string::npos;
}

uint32_t SohUtils::Hash(std::string str) {
    // FNV-1a
    const size_t len = str.size();
    uint32_t hval = 0x811c9dc5;
    for (size_t pos = 0; pos < len; pos++) {
        hval ^= (uint32_t)str[pos];
        hval *= 0x01000193;
    }
    return hval;
}

std::vector<std::string> SohUtils::StringSplit(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t pos = str.find(delimiter, 0);
    size_t prevpos = 0;

    while (pos != std::string::npos) {
        std::string token = str.substr(prevpos, pos - prevpos);
        tokens.push_back(token);
        prevpos = pos + 1;
        pos = str.find(delimiter, prevpos);
    }

    tokens.push_back(str.substr(prevpos));

    return tokens;
}