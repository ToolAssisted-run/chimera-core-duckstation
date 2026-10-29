// chimera-core-duckstation: the arcade boards.
//
// What makes the machine one of the boards built out of a PlayStation: a MAME
// rom set instead of a disc, the board's own firmware instead of the
// console's, and a panel - sticks, buttons, a coin slot, a gun - instead of
// controller ports. The emulator underneath is ArcadeDuck (extern/arcadeduck),
// which knows each board; this file is how a project reaches it. Included by
// cinterface.cpp when that is compiled against the arcade tree; the console's
// half is machine-console.inc.

// patches-arcade: "what a board keeps is the machine's memory"
extern "C" int ChimeraKeptCount(void);
extern "C" const char* ChimeraKeptName(int i);
extern "C" void* ChimeraKeptData(int i, size_t* size);

namespace {

// ---- the wire ----------------------------------------------------------------

// The arcade machines' own wire (waterbox.config, "machines"), in its order:
// per player every control ArcadeDuck's boards read - a stick, Start, a coin
// slot, six buttons, a gun's trigger and reload, a driving cabinet's shift,
// handbrake, view, horn and music buttons, and the select and sensor buttons
// of Konami's stranger cabinets - then the two switches inside the cabinet
// door. The names on the right are ArcadeDuck's own for them.
constexpr int PORTS = 4;
constexpr int PORT_BUTTONS = 30;
constexpr int PORT_AXES = 8;
constexpr const char* kControl[PORT_BUTTONS] = {
  "Up",       "Down",     "Left",       "Right",       "Start",      "Coin",          "Button1",  "Button2",
  "Button3",  "Button4",  "Button5",    "Button6",     "Trigger",    "Reload",        "GearUp",   "GearDown",
  "Handbrake", "View",    "Horn",       "MusicNext",   "MusicPrevious", "Select",     "SelectUp", "SelectDown",
  "SelectLeft", "SelectRight", "Enter", "Sensor",      "GSRIncrease", "GSRDecrease",
};
enum : int
{
  A_GUN_X, A_GUN_Y,                           // 0..65535 across the picture and down it
  A_STEERING, A_ACCELERATOR, A_BRAKE, A_CLUTCH, // 0..65535; the wheel 32768 straight, a pedal 0 released
  A_BALL_X, A_BALL_Y,                         // how far a trackball rolled this frame
};
constexpr s32 kAxisRest[PORT_AXES] = {32768, 32768, 32768, 0, 0, 0, 0, 0};
constexpr int WIRE_OPERATOR = PORTS * PORT_BUTTONS;
constexpr int WIRE_SERVICE = WIRE_OPERATOR + 0;
constexpr int WIRE_TEST = WIRE_OPERATOR + 1;
constexpr int WIRE_COUNT = WIRE_OPERATOR + 2;

// which of the wire this game's panel has (decided at boot, from ArcadeDuck's
// own record of the cabinet)
bool g_live[WIRE_COUNT];
Arcade::ArcadeControllerType g_panel[PORTS]; // what each player's part of the panel is
s32 g_axis[PORTS][PORT_AXES];                // each axis as the movie last said it
bool g_panelProbe;

const Arcade::Database::GameDefinition* g_game;
char g_set[512];

int g_driveLight;

// ---- the hooks cinterface.cpp calls ---------------------------------------------

void MachineApplySettings(SettingsInterface& si)
{
  // An arcade board is the machine it is: its region is the rom set's, and
  // its firmware is the archive the board's kind names. Both are read by
  // ArcadeDuck from the files the project mounted, at the root.
  si.SetStringValue("BIOS", "SearchDirectory", "");
  si.SetStringValue("Console", "Region", Settings::GetConsoleRegionName(ConsoleRegion::Auto));
}

// The panel: what ArcadeDuck's database says this cabinet has, port by port,
// said to its input code the way its own frontend would say it (a controller
// type and a layout per port) - and remembered here as the live wires.
void DescribePanel(SettingsInterface& si)
{
  std::memset(g_live, 0, sizeof(g_live));
  g_live[WIRE_SERVICE] = true;
  g_live[WIRE_TEST] = true;

  // "arcade_panel_probe" is a question put to the core, as "arcadedb_probe"
  // is: not in waterbox.config, never in a movie. It makes player 1's part of
  // the panel a driving cabinet's or a trackball, whatever the game has, and
  // says on stderr what ArcadeDuck's input code then holds - which is how a
  // wheel and pedals are checked on a machine with no driving game to run.
  char probe[16] = "";
  wbx_setting_str("arcade_panel_probe", probe, sizeof(probe));
  g_panelProbe = (probe[0] != '\0');

  for (int port = 0; port < PORTS; port++)
  {
    Arcade::Database::PortDefinition def = g_game->ports[port];
    if (port == 0 && g_panelProbe)
    {
      def.controller_type = (strcmp(probe, "trackball") == 0) ? Arcade::ArcadeControllerType::Trackball :
                                                                 Arcade::ArcadeControllerType::Driving;
      def.layout.clear();
    }
    g_panel[port] = Arcade::ArcadeControllerType::None;
    std::memcpy(g_axis[port], kAxisRest, sizeof(kAxisRest));
    const std::string section = "ArcadeControllerPort" + std::to_string(port + 1);
    const Arcade::ArcadeControllerTypeInfo* const info = Arcade::GetArcadeControllerTypeInfo(def.controller_type);
    if (def.controller_type == Arcade::ArcadeControllerType::None || !info)
    {
      si.SetStringValue(section.c_str(), "Type", "none");
      continue;
    }
    g_panel[port] = def.controller_type;

    si.SetStringValue(section.c_str(), "Type", std::string(info->name).c_str());
    si.SetStringValue(section.c_str(), "Layout", def.layout.c_str());
    // A gun is aimed where the movie says, exactly: no host pointer, no
    // sensitivity, and "off the screen" only when the movie says that too.
    si.SetStringValue(section.c_str(), "InputMode", "BoundAxis");
    si.SetBoolValue(section.c_str(), "OffscreenReload", true);
    si.SetBoolValue(section.c_str(), "CrosshairEnabled", false);

    // The layout names the controls this cabinet has. One with no layout on
    // record has the whole port.
    const Arcade::Database::ControlLayoutDefinition* const layout = Arcade::Database::GetControlLayout(def.layout);
    for (int k = 0; k < PORT_BUTTONS; k++)
    {
      bool has = (layout == nullptr);
      if (layout)
      {
        for (const Arcade::Database::ControlLayoutBindingDefinition& b : layout->bindings)
          has = has || (b.key == kControl[k]);
      }
      // a coin slot and a Start button are the cabinet's, not the layout's
      if (k == 4 || k == 5)
        has = true;
      g_live[port * PORT_BUTTONS + k] = has;
    }
  }
}

// ---- what the board keeps -------------------------------------------------------

// A board's non-volatile memory - an EEPROM, a flash card - is its own array,
// declared by name as the board starts (patches-arcade: "what a board keeps is
// the machine's memory"). It is in every state and rewinds with the game. What
// a project starts with comes in through its Save Data slot under those
// names, and what a session ends with leaves through the save-data exports
// under the same names, so one round-trips into the other.
// The name a kept array travels under: its own, and ".nv" so that a file
// dialog has something to filter by (waterbox/file_slots.json, Board memory).
const char* KeptFileName(int k)
{
  static char name[64];
  snprintf(name, sizeof(name), "%s.nv", ChimeraKeptName(k));
  return name;
}

bool SeedWhatTheBoardKeeps()
{
  char entry[512];
  const int saves = wbx_slot_count("boardmemory");
  for (int i = 0; i < saves; i++)
  {
    if (wbx_slot_name("boardmemory", i, entry, sizeof(entry)) == nullptr)
      break;

    int which = -1;
    for (int k = 0; k < ChimeraKeptCount(); k++)
    {
      if (std::strcmp(entry, KeptFileName(k)) == 0)
        which = k;
    }
    if (which < 0)
    {
      std::string names;
      for (int k = 0; k < ChimeraKeptCount(); k++)
        names += std::string(k ? ", " : "") + KeptFileName(k);
      snprintf(g_loadError, sizeof(g_loadError),
               names.empty() ? "error.board_keeps_nothing" TEXT_ARG "%s" : "error.board_keeps" TEXT_ARG "%s" TEXT_ARG "%s", entry, names.c_str());
      return false;
    }

    size_t size = 0;
    void* const memory = ChimeraKeptData(which, &size);
    std::FILE* const f = std::fopen(entry, "rb");
    u8 extra;
    const bool ok = f && memory && std::fread(memory, 1, size, f) == size && std::fread(&extra, 1, 1, f) == 0;
    if (f)
      std::fclose(f);
    if (!ok)
    {
      snprintf(g_loadError, sizeof(g_loadError),
               "error.board_memory_size" TEXT_ARG "%s" TEXT_ARG "%s" TEXT_ARG "%zu", entry, entry, size);
      return false;
    }
  }
  return true;
}

bool MachineBoot(Error* error)
{
  if (!Arcade::Database::EnsureLoaded())
  {
    snprintf(g_loadError, sizeof(g_loadError), "error.no_board_record");
    return false;
  }

  // "arcadedb_probe" is a question put to the core, as "gamedb_probe" is to
  // the console: not in waterbox.config, changing nothing, and never in a movie.
  {
    char probe[32] = "";
    if (wbx_setting_str("arcadedb_probe", probe, sizeof(probe)) > 0)
    {
      const Arcade::Database::GameDefinition* const entry = Arcade::Database::GetGame(probe);
      std::fprintf(stderr, "duckstation: arcadedb probe: %s = %s\n", probe,
                   entry ? (entry->title + " [" + entry->system_id + "]").c_str() : "(not on record)");
    }
  }

  // The rom set: the first .zip in the project's Rom Set slot. A game's hard
  // disk or CD, and a clone's parent, are mounted beside it under their own
  // names, which is where ArcadeDuck looks for them.
  g_set[0] = '\0';
  {
    char entry[512];
    const int n = wbx_slot_count("romset");
    for (int i = 0; i < n && g_set[0] == '\0'; i++)
    {
      if (wbx_slot_name("romset", i, entry, sizeof(entry)) == nullptr)
        break;
      const size_t len = std::strlen(entry);
      if (len > 4 && strcasecmp(entry + len - 4, ".zip") == 0)
        snprintf(g_set, sizeof(g_set), "%s", entry);
    }
  }
  if (g_set[0] == '\0')
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "error.no_set");
    return false;
  }

  g_game = Arcade::Database::IdentifyArchive(g_set);
  if (!g_game)
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "error.unknown_set" TEXT_ARG "%s",
             g_set);
    return false;
  }

  // The machine is the project's, not the file's: a set for another board is
  // refused, the way a disc for another console would be.
  {
    char want[32] = "";
    wbx_setting_str("machine", want, sizeof(want));
    if (want[0] != '\0' && g_game->system_id != want)
    {
      snprintf(g_loadError, sizeof(g_loadError),
               "error.wrong_board" TEXT_ARG "%s" TEXT_ARG "%s" TEXT_ARG "%s",
               g_set, g_game->system_id.c_str(), want);
      return false;
    }
  }

  DescribePanel(*g_settingsLayer);
  ArcadeInput::RegisterBindings(*g_settingsLayer, *g_settingsLayer);

  // Not ArcadeDuck's "public" launch: that one resolves the archive's real
  // path on the host first, and a sandbox's files have none. The set was
  // identified above, by the same lookup.
  SystemBootParameters params(g_set);
  params.override_fullscreen = false;
  params.override_start_paused = false;
  if (!System::BootSystem(std::move(params), error))
  {
    Fail("error.board_would_not_start", *error);
    return false;
  }
  return SeedWhatTheBoardKeeps();
}

void MachineBeforeFrame()
{
  // A trackball's roll is so much per frame: given to the board once a frame.
  for (int port = 0; port < PORTS; port++)
  {
    if (g_panel[port] == Arcade::ArcadeControllerType::Trackball &&
        (g_axis[port][A_BALL_X] != 0 || g_axis[port][A_BALL_Y] != 0))
    {
      ArcadeInput::ChimeraAddTrackball(static_cast<u32>(port), g_axis[port][A_BALL_X], g_axis[port][A_BALL_Y]);
    }
  }
}

void MachineAfterFrame()
{
  g_driveLight = g_driveLightNow;
}

} // namespace

// ---- input ----------------------------------------------------------------------

static void SetButton(int32_t index, int32_t state)
{
  if (index < 0 || index >= WIRE_COUNT || !g_live[index])
    return;
  if (index == WIRE_SERVICE)
    ArcadeInput::ChimeraSetOperator("Service", state != 0);
  else if (index == WIRE_TEST)
    ArcadeInput::ChimeraSetOperator("Test", state != 0);
  else
    ArcadeInput::ChimeraSetDigital(static_cast<u32>(index / PORT_BUTTONS), kControl[index % PORT_BUTTONS], state != 0);
}

/* Which of the declared wires this cabinet has. */
static int IsButtonActive(int32_t index)
{
  return (index >= 0 && index < WIRE_COUNT && g_live[index]) ? 1 : 0;
}

static bool HasAxis(int port, int axis)
{
  switch (g_panel[port])
  {
    case Arcade::ArcadeControllerType::Lightgun:
      return axis == A_GUN_X || axis == A_GUN_Y;
    case Arcade::ArcadeControllerType::Driving:
      return axis >= A_STEERING && axis <= A_CLUTCH;
    case Arcade::ArcadeControllerType::Trackball:
      return axis == A_BALL_X || axis == A_BALL_Y;
    default:
      return false;
  }
}

static int IsAxisActive(int32_t index)
{
  return (index >= 0 && index < PORTS * PORT_AXES && HasAxis(index / PORT_AXES, index % PORT_AXES)) ? 1 : 0;
}

/* A gun's aim: 0..65535 across the picture and down it. A wheel: 0 hard left,
 * 65535 hard right; a pedal: 0 released, 65535 to the floor. A trackball: how
 * far it rolled this frame. */
static void SetAxis(int32_t index, int32_t value)
{
  if (index < 0 || index >= PORTS * PORT_AXES)
    return;
  const int port = index / PORT_AXES;
  const int axis = index % PORT_AXES;
  if (!HasAxis(port, axis))
    return;

  if (axis == A_BALL_X || axis == A_BALL_Y)
  {
    g_axis[port][axis] = std::clamp<int32_t>(value, -127, 127);
    return;
  }

  const s32 was = g_axis[port][axis];
  g_axis[port][axis] = std::clamp<int32_t>(value, 0, 65535);
  if (axis == A_GUN_X || axis == A_GUN_Y)
  {
    ArcadeInput::ChimeraSetGun(static_cast<u32>(port), static_cast<float>(g_axis[port][A_GUN_X]) / 65535.0f,
                               static_cast<float>(g_axis[port][A_GUN_Y]) / 65535.0f, false);
    return;
  }

  ArcadeInput::ChimeraSetDriving(static_cast<u32>(port), static_cast<float>(g_axis[port][A_STEERING]) / 65535.0f,
                                 static_cast<float>(g_axis[port][A_ACCELERATOR]) / 65535.0f,
                                 static_cast<float>(g_axis[port][A_BRAKE]) / 65535.0f,
                                 static_cast<float>(g_axis[port][A_CLUTCH]) / 65535.0f);
  if (g_panelProbe && was != g_axis[port][axis])
  {
    // what ArcadeDuck's own readers are given, in sixty-five-thousandths
    const u32 p = static_cast<u32>(port);
    std::fprintf(stderr, "duckstation: panel probe: P%d steering %d accelerator %d brake %d clutch %d\n", port + 1,
                 static_cast<int>(ArcadeInput::GetAnalogValue(p, "Steering") * 65535.0f + 0.5f),
                 static_cast<int>(ArcadeInput::GetAnalogValue(p, "Accelerator") * 65535.0f + 0.5f),
                 static_cast<int>(ArcadeInput::GetAnalogValue(p, "Brake") * 65535.0f + 0.5f),
                 static_cast<int>(ArcadeInput::GetAnalogValue(p, "Clutch") * 65535.0f + 0.5f));
  }
}

// ---- save data: what the board keeps ------------------------------------------------

static int32_t GetSaveDataFileCount(void)
{
  return g_booted ? ChimeraKeptCount() : 0;
}
static const char* GetSaveDataFileName(int32_t i)
{
  return (i >= 0 && i < ChimeraKeptCount()) ? KeptFileName(i) : "";
}
static int64_t GetSaveDataFileSize(int32_t i)
{
  size_t size;
  ChimeraKeptData(i, &size);
  return static_cast<int64_t>(size);
}
static const uint8_t* GetSaveDataFileBuffer(int32_t i)
{
  size_t size;
  return static_cast<const uint8_t*>(ChimeraKeptData(i, &size));
}

// ---- the drive: a board's disc is bolted in -------------------------------------

static int32_t GetDriveCount(void)
{
  return 0;
}
static const char* GetDriveName(int32_t i)
{
  return "";
}
static int32_t GetDriveLight(int32_t i)
{
  return g_driveLight;
}
static int32_t GetDriveMediaCount(int32_t i)
{
  return 0;
}
static const char* GetDriveMediaName(int32_t i, int32_t n)
{
  return "";
}
static int32_t GetDriveMediaSelected(int32_t i)
{
  return 0;
}
static int32_t GetDriveMediaInserted(int32_t i)
{
  return -1;
}
