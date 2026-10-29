// chimera-core-duckstation: the console.
//
// What makes the machine a PlayStation and not one of the boards built out of
// one: two controller ports and what plugs into them, a lid with discs under
// it, memory cards, a Reset button. Included by cinterface.cpp, which is the
// rest of the adapter and is compiled once for each of the two trees
// (meson.build, "one binary, two machines"); the arcade boards' half is
// machine-arcade.inc.

namespace {

// ---- the wire ----------------------------------------------------------------

// What waterbox.config declares, in its order: eight sockets, and for each
// every control any controller this console had could put there. The sockets
// are upstream's own pads, in its order - port 1, port 2, then the three more
// a multitap in port 1 gives (1B 1C 1D), then port 2's (2B 2C 2D) - so P1 and
// P2 are the two ports whether or not a multitap is plugged in. What a socket
// actually holds is a setting, and only that controller's wires are live.
constexpr int PORTS = 8;
constexpr int PORT_BUTTONS = 25;
constexpr int PORT_AXES = 12;
enum : int
{
  // a digital pad's fourteen, then what a DualShock or an analog joystick adds
  W_UP, W_DOWN, W_LEFT, W_RIGHT, W_START, W_SELECT, W_SQUARE, W_CROSS, W_CIRCLE, W_TRIANGLE,
  W_L1, W_R1, W_L2, W_R2, W_L3, W_R3, W_ANALOG,
  // a NeGcon's A, B and R (its I, II and L are axes); a GunCon's A and B
  W_A, W_B, W_R,
  // a light gun's trigger, the shot aimed away from the screen, and the
  // Justifier's button at the back
  W_TRIGGER, W_OFFSCREEN, W_BACK,
  // the mouse
  W_MOUSE_LEFT, W_MOUSE_RIGHT,
};
enum : int
{
  A_LX, A_LY, A_RX, A_RY,       // a stick byte: 0..255, 128 at rest
  A_TWIST, A_I, A_II, A_L,      // a NeGcon: the twist (128 at rest) and three analog buttons (0 at rest)
  A_GUN_X, A_GUN_Y,             // where a gun is aimed: 0..65535 across the picture and down it
  A_MOUSE_X, A_MOUSE_Y,         // how far the mouse moved this frame
};
constexpr int WIRE_ANALOG = W_ANALOG;

// wire -> upstream's bind index, for each kind of controller; -1 where the
// controller has no such control. A pad's are the same numbers for the
// digital pad, the DualShock and the analog joystick, which is the order the
// pad's own report has them in.
constexpr s8 kPadBind[PORT_BUTTONS] = {
  4, 6, 7, 5, 3, 0, 15, 14, 13, 12, 10, 11, 8, 9, 1, 2, 16, -1, -1, -1, -1, -1, -1, -1, -1,
};
constexpr s8 kNeGconBind[PORT_BUTTONS] = {
  1, 3, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7, 6, 5, -1, -1, -1, -1, -1,
};
constexpr s8 kGunConBind[PORT_BUTTONS] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 2, -1, 0, 3, -1, -1, -1,
};
constexpr s8 kJustifierBind[PORT_BUTTONS] = {
  -1, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 3, 2, -1, -1,
};
constexpr s8 kMouseBind[PORT_BUTTONS] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 1,
};

// The bind a wire is on the controller that is actually there (a setting, and
// upstream's game database on top of it), or -1: the wire is not live.
int BindOf(ControllerType type, int wire)
{
  switch (type)
  {
    case ControllerType::DigitalController:
      return (wire <= W_R2) ? kPadBind[wire] : -1;
    case ControllerType::AnalogController:
    case ControllerType::AnalogJoystick:
      return kPadBind[wire];
    case ControllerType::NeGcon:
    case ControllerType::NeGconRumble:
      return kNeGconBind[wire];
    case ControllerType::GunCon:
      return kGunConBind[wire];
    case ControllerType::Justifier:
      return kJustifierBind[wire];
    case ControllerType::PlayStationMouse:
      return kMouseBind[wire];
    default:
      return -1;
  }
}

bool HasAxis(ControllerType type, int axis)
{
  switch (type)
  {
    case ControllerType::AnalogController:
    case ControllerType::AnalogJoystick:
      return axis <= A_RY;
    case ControllerType::NeGcon:
    case ControllerType::NeGconRumble:
      return axis >= A_TWIST && axis <= A_L;
    case ControllerType::GunCon:
    case ControllerType::Justifier:
      return axis == A_GUN_X || axis == A_GUN_Y;
    case ControllerType::PlayStationMouse:
      return axis == A_MOUSE_X || axis == A_MOUSE_Y;
    default:
      return false;
  }
}

ControllerType g_portType[PORTS];
u8 g_held[PORTS][PORT_BUTTONS];
// each axis as the movie last said it, and the value it rests at
constexpr s32 kAxisRest[PORT_AXES] = {128, 128, 128, 128, 128, 0, 0, 0, 32768, 32768, 0, 0};
s32 g_axis[PORTS][PORT_AXES];
bool g_axisSaid;

// After the pads, the console's own controls: its Reset button, and the lid.
constexpr int WIRE_CONSOLE = PORTS * PORT_BUTTONS;
constexpr int WIRE_RESET = WIRE_CONSOLE + 0;
constexpr int WIRE_PREV_DISC = WIRE_CONSOLE + 1;
constexpr int WIRE_NEXT_DISC = WIRE_CONSOLE + 2;
constexpr int WIRE_DISC_SWAP = WIRE_CONSOLE + 3;
constexpr int WIRE_COUNT = WIRE_CONSOLE + 4;
u8 g_console[4];     // as held this frame
u8 g_consoleWas[4];  // as held the frame before

// ---- the discs ---------------------------------------------------------------

// Every disc the project put in its Disc slot, in the slot's order, which is
// the order the selector steps through. All of it is the machine's - which
// disc is in the drive, which one the selector is on, whether the lid is up -
// and travels in a state, so a movie that changes discs replays the change.
constexpr int MAX_DISCS = 8;
char g_discs[MAX_DISCS][512];
int g_discCount;
int g_discInserted = -1; // -1: the lid is up, or the tray was empty all along
int g_discWasInserted;   // the disc that was in when the lid went up
int g_discSelected;
bool g_lidUp;
int g_driveLight;        // the drive read a sector this frame

void DiscControls()
{
  const bool prev = g_console[1] && !g_consoleWas[1];
  const bool next = g_console[2] && !g_consoleWas[2];
  if (g_discCount > 1)
  {
    // PREVIOUS and NEXT are a selector: they choose, and touch no hardware.
    // They wrap both ways (chimera issue 47: a selector that ran off the end
    // could never reach the second disc again).
    if (prev)
      g_discSelected = (g_discSelected + g_discCount - 1) % g_discCount;
    if (next)
      g_discSelected = (g_discSelected + 1) % g_discCount;
    if ((prev || next) && chimera_verbose)
      std::fprintf(stderr, "duckstation: disc selector: %d of %d (%s)\n", g_discSelected + 1, g_discCount,
                   g_discs[g_discSelected]);
  }

  // DISC SWAP is the lid. Held, it is up: the drive reports the shell open
  // and no disc, which is what a game asking for its next disc waits to see.
  // Released, the SELECTED disc is in and the drive spins it up. Both in one
  // frame would be a lid that never opened as far as a polling game can
  // tell, so how long it stays up is how long the button is held - the
  // player's decision, and the movie's. With the selector untouched the lid
  // closes on the next disc, so the button alone steps through them.
  const bool up = g_console[3] != 0;
  if (up == g_lidUp || g_discCount == 0)
    return;

  g_lidUp = up;
  if (up)
  {
    System::RemoveMedia();
    g_discInserted = -1;
    return;
  }

  if (g_discCount > 1 && g_discSelected == g_discWasInserted)
    g_discSelected = (g_discWasInserted + 1) % g_discCount;
  if (System::InsertMedia(g_discs[g_discSelected]))
  {
    g_discInserted = g_discSelected;
    g_discWasInserted = g_discSelected;
  }
  else
  {
    // the lid stays up on an empty tray: a drive that believes it holds a
    // disc it cannot read is worse than one that knows it is empty
    g_lidUp = true;
    std::fprintf(stderr, "duckstation: disc %d (%s) will not open; the lid stays up\n", g_discSelected + 1,
                 g_discs[g_discSelected]);
  }
}

// ---- memory cards -------------------------------------------------------------

// A card is 128KB of the machine's own memory (upstream's non-persistent
// kind): it is in every state, it rewinds with the game that wrote it, and
// nothing reaches a disk. What a project starts with comes in through its
// Save Data slot under the names below, and what a session ends with leaves
// through the save-data exports under the same names, so one round-trips
// into the other.
constexpr int CARDS = 2;
constexpr const char* kCardNames[CARDS] = {"memcard1.mcd", "memcard2.mcd"};
bool g_cardPresent[CARDS];

// The card image a project brought, into the card the console booted with.
// False, with g_loadError set, for a file that is not a card.
bool SeedCard(int slot)
{
  if (!FileSystem::FileExists(kCardNames[slot]))
    return true;

  MemoryCard* const card = Pad::GetMemoryCard(static_cast<u32>(slot));
  if (!card)
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "error.card_off" TEXT_ARG "%s" TEXT_ARG "%d",
             kCardNames[slot], slot + 1);
    return false;
  }

  std::FILE* const f = std::fopen(kCardNames[slot], "rb");
  MemoryCardImage::DataArray& data = card->GetData();
  u8 extra;
  const bool ok = f && std::fread(data.data(), 1, data.size(), f) == data.size() && std::fread(&extra, 1, 1, f) == 0;
  if (f)
    std::fclose(f);
  if (!ok || !MemoryCardImage::IsValid(data))
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "error.not_card" TEXT_ARG "%s",
             kCardNames[slot]);
    return false;
  }
  return true;
}

ControllerType ParsePort(const char* key, ControllerType dflt)
{
  static const struct { const char* ours; ControllerType type; } kTypes[] = {
    {"none", ControllerType::None},
    {"digital", ControllerType::DigitalController},
    {"dualshock", ControllerType::AnalogController},
    {"analogJoystick", ControllerType::AnalogJoystick},
    {"negcon", ControllerType::NeGcon},
    {"negconRumble", ControllerType::NeGconRumble},
    {"guncon", ControllerType::GunCon},
    {"justifier", ControllerType::Justifier},
    {"mouse", ControllerType::PlayStationMouse},
  };
  char v[24] = "";
  if (wbx_setting_str(key, v, sizeof(v)) < 0)
    return dflt;
  for (const auto& t : kTypes)
  {
    if (!strcmp(v, t.ours))
      return t.type;
  }
  return dflt;
}

// One stick byte, put where the pad will report it from. Upstream's pad takes
// each direction of a stick as a separate half axis, scales it to 0..255 and
// merges the pair: 127 + (positive + 1) / 2, or 127 - negative / 2. Inverted,
// every byte has one pair that produces it, and the half-unit added below is
// what makes the float survive the truncation on the other side.
void SetStickByte(Controller* c, u32 positive, u32 negative, u8 value)
{
  const u32 pos = (value >= 128) ? (2u * value - 255u) : 0u;
  const u32 neg = (value >= 128) ? 0u : (2u * (127u - value));
  const u32 first = 17u; // AnalogController::Button::Count, and AnalogJoystick's
  c->SetBindState(first + negative, (static_cast<float>(neg) + 0.5f) / 255.0f);
  c->SetBindState(first + positive, (static_cast<float>(pos) + 0.5f) / 255.0f);
}

// A NeGcon's twist, as the byte it reports: 128 straight, 0 hard left, 255
// hard right. Upstream takes the two directions as half axes and reports
// round(128 * (right - left) + 128), so byte b is (b - 128) / 128 on one side
// and nothing on the other - quotients a float holds exactly.
void SetTwistByte(Controller* c, u8 value)
{
  const u32 first = 8u; // NeGcon::Button::Count; then SteeringLeft, SteeringRight, I, II, L
  const float left = (value < 128) ? (static_cast<float>(128 - value) / 128.0f) : 0.0f;
  const float right = (value > 128) ? (static_cast<float>(value - 128) / 128.0f) : 0.0f;
  c->SetBindState(first + 0u, left);
  c->SetBindState(first + 1u, right);
}

// A gun's aim. Upstream has no absolute axis for it: with no host pointer it
// takes four half axes ("relative" binds) and makes of them a position from
// 0 to 1 across the picture and down it (patches: "a gun is aimed at the
// picture"). The movie's 0..65535 is that fraction.
void SetGunAim(Controller* c, s32 x, s32 y)
{
  const float sx = (static_cast<float>(x) / 65535.0f) * 2.0f - 1.0f;
  const float sy = (static_cast<float>(y) / 65535.0f) * 2.0f - 1.0f;
  // Binding::RelativeLeft, Right, Up, Down - the same four for both guns
  c->SetBindState(4u, (sx < 0.0f) ? -sx : 0.0f);
  c->SetBindState(5u, (sx > 0.0f) ? sx : 0.0f);
  c->SetBindState(6u, (sy < 0.0f) ? -sy : 0.0f);
  c->SetBindState(7u, (sy > 0.0f) ? sy : 0.0f);
}

// ---- a file with no name ------------------------------------------------------

// A file the frontend was handed directly - dropped on it, or named on a
// command line - is mounted as "disc": no project, no slot map, no extension.
// DuckStation decides what an image is by its extension, so the bytes are
// asked instead and the file is given the name it should have had. Returns
// the name to boot, or null with g_loadError saying why not.
const char* NameTheBareFile()
{
  static char name[32];
  u8 head[16] = {};
  std::FILE* f = std::fopen("disc", "rb");
  if (!f)
    return nullptr;
  const size_t got = std::fread(head, 1, sizeof(head), f);
  std::fclose(f);

  // A project with nothing in its Disc slot still mounts the name, with
  // nothing in it: that is an empty tray, not a file to be refused.
  if (got == 0)
    return nullptr;

  static const u8 kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  const char* ext;
  if (got >= 8 && !std::memcmp(head, "MComprHD", 8))
    ext = "chd";
  else if (got >= 8 && !std::memcmp(head, "PS-X EXE", 8))
    ext = "exe";
  else if (got >= 4 && !std::memcmp(head, "\0PBP", 4))
    ext = "pbp";
  else if (got >= 4 && !std::memcmp(head, "ECM\0", 4))
    ext = "ecm";
  else if (got >= 12 && !std::memcmp(head, kSync, 12))
    ext = "bin";
  else
  {
    // Not a raw sector and nothing with a signature. A cue sheet is the
    // likely one: text, and useless alone, because the files it names were
    // not mounted with it.
    bool text = (got > 0);
    for (size_t i = 0; i < got; i++)
      text = text && (head[i] == 0x09 || head[i] == 0x0A || head[i] == 0x0D || (head[i] >= 0x20 && head[i] < 0x7F) ||
                      head[i] >= 0x80);
    snprintf(g_loadError, sizeof(g_loadError),
             text ? "error.cue_alone" : "error.not_disc");
    return nullptr;
  }

  snprintf(name, sizeof(name), "disc.%s", ext);
  ChimeraFileAlias(name, "disc");
  return name;
}

// ---- the hooks cinterface.cpp calls ---------------------------------------------

void MachineApplySettings(SettingsInterface& si)
{
  // The widescreen hack: the geometry processor is told the screen is wider,
  // and computes a wider view. The raster stays the console's; showing it at
  // that shape is the frontend's. It changes what is drawn, and so how long
  // the video chip is busy - the machine, not only the picture.
  {
    char v[8] = "off";
    wbx_setting_str("widescreen", v, sizeof(v));
    const bool wide = (strcmp(v, "16:9") == 0 || strcmp(v, "19:9") == 0 || strcmp(v, "20:9") == 0);
    si.SetBoolValue("GPU", "WidescreenHack", wide);
    if (wide)
      si.SetStringValue("Display", "AspectRatio", v);
  }

  // A multitap turns one port into four sockets. Which ports have one is the
  // console's shape, and so a setting.
  bool tap[2] = {false, false};
  {
    char v[16] = "none";
    wbx_setting_str("multitap", v, sizeof(v));
    tap[0] = (strcmp(v, "port1") == 0 || strcmp(v, "both") == 0);
    tap[1] = (strcmp(v, "port2") == 0 || strcmp(v, "both") == 0);
    si.SetStringValue("ControllerPorts", "MultitapMode",
                      Settings::GetMultitapModeName(tap[0] ? (tap[1] ? MultitapMode::BothPorts : MultitapMode::Port1Only) :
                                                             (tap[1] ? MultitapMode::Port2Only : MultitapMode::Disabled)));
  }

  // What is in each socket. A DualShock in the first port and nothing in the
  // second is what a fresh DuckStation has; a game its database says cannot
  // use a DualShock is given the digital pad instead, by upstream, at boot. A
  // socket of a multitap that is not there holds nothing, whatever is said.
  static const char* const kPortSetting[PORTS] = {"port1",  "port2",  "port1b", "port1c",
                                                  "port1d", "port2b", "port2c", "port2d"};
  for (int port = 0; port < PORTS; port++)
  {
    g_portType[port] = ParsePort(kPortSetting[port], (port == 0) ? ControllerType::AnalogController : ControllerType::None);
    if (port >= 2 && !tap[(port >= 5) ? 1 : 0])
      g_portType[port] = ControllerType::None;
  }
  for (int port = 0; port < PORTS; port++)
  {
    const std::string section = Controller::GetSettingsSection(static_cast<u32>(port));
    si.SetStringValue(section.c_str(), "Type", Controller::GetControllerInfo(g_portType[port])->name);
    // A gun is aimed where the movie says: upstream then reads its "relative"
    // half axes instead of a host pointer, which it does when a binding for
    // one exists. The binding names nothing; the adapter sets the axes.
    if (g_portType[port] == ControllerType::GunCon || g_portType[port] == ControllerType::Justifier)
    {
      si.SetStringValue(section.c_str(), "RelativeLeft", "Chimera/Aim");
      si.SetFloatValue(section.c_str(), "XScale", 1.0f);
    }
    // A NeGcon's twist and its analog buttons are the bytes the movie says: no
    // dead zone, no saturation short of the end, no curve.
    for (const char* axis : {"Steering", "I", "II", "L"})
    {
      si.SetFloatValue(section.c_str(), (std::string(axis) + "Deadzone").c_str(), 0.0f);
      si.SetFloatValue(section.c_str(), (std::string(axis) + "Saturation").c_str(), 1.0f);
      si.SetFloatValue(section.c_str(), (std::string(axis) + "Linearity").c_str(), 0.0f);
      si.SetFloatValue(section.c_str(), (std::string(axis) + "Scaling").c_str(), 1.0f);
    }
    // a count of the mouse is a count
    si.SetFloatValue(section.c_str(), "SensitivityX", 1.0f);
    si.SetFloatValue(section.c_str(), "SensitivityY", 1.0f);
    // A stick byte is the byte the movie says: no curve, no dead zone, and no
    // stick standing in for the d-pad - that last one is an emulator's
    // convenience, and a pad in digital mode does not report its sticks.
    si.SetFloatValue(section.c_str(), "AnalogSensitivity", 1.0f);
    si.SetFloatValue(section.c_str(), "AnalogDeadzone", 0.0f);
    si.SetBoolValue(section.c_str(), "AnalogDPadInDigitalMode", false);
    si.SetIntValue(section.c_str(), "InvertLeftStick", 0);
    si.SetIntValue(section.c_str(), "InvertRightStick", 0);
  }
  // Memory cards: in the machine's own memory, or not plugged in.
  g_cardPresent[0] = wbx_setting_bool("memcard1", 1) != 0;
  g_cardPresent[1] = wbx_setting_bool("memcard2", 0) != 0;
  si.SetStringValue("MemoryCards", "Card1Type",
                    Settings::GetMemoryCardTypeName(g_cardPresent[0] ? MemoryCardType::NonPersistent :
                                                                       MemoryCardType::None));
  si.SetStringValue("MemoryCards", "Card2Type",
                    Settings::GetMemoryCardTypeName(g_cardPresent[1] ? MemoryCardType::NonPersistent :
                                                                       MemoryCardType::None));


  // Which market the console was made for. "auto" is upstream's own where
  // there is a disc: the disc decides. With an empty tray the ROM does (Init).
  {
    char region[16] = "auto";
    wbx_setting_str("region", region, sizeof(region));
    ConsoleRegion r = ConsoleRegion::Auto;
    if (!strcmp(region, "ntsc-u"))
      r = ConsoleRegion::NTSC_U;
    else if (!strcmp(region, "ntsc-j"))
      r = ConsoleRegion::NTSC_J;
    else if (!strcmp(region, "pal"))
      r = ConsoleRegion::PAL;
    si.SetStringValue("Console", "Region", Settings::GetConsoleRegionName(r));
  }

  // one firmware, whatever the region: the project chose it
  si.SetStringValue("BIOS", "SearchDirectory", "");
  si.SetStringValue("BIOS", "PathNTSCU", "bios");
  si.SetStringValue("BIOS", "PathNTSCJ", "bios");
  si.SetStringValue("BIOS", "PathPAL", "bios");
}

// Everything between "the emulator is up" and "the machine is running": the
// ROM, the disc, the cards. False with g_loadError set.
bool MachineBoot(Error* error)
{
  // The per-title database, loaded HERE rather than where upstream first asks
  // for it: before the guest is sealed, so it costs nothing in every state
  // afterwards, and before the machine is asked for anything else, so a core
  // with an EMPTY database can be caught with no BIOS and no disc at all.
  // "gamedb_probe" is a question put to the core, not a setting: it is not in
  // waterbox.config, it changes nothing, and a movie must not carry it.
  GameDatabase::EnsureLoaded();
  {
    char probe[32] = "";
    if (wbx_setting_str("gamedb_probe", probe, sizeof(probe)) > 0)
    {
      const GameDatabase::Entry* const entry = GameDatabase::GetEntryForSerial(probe);
      std::fprintf(stderr, "duckstation: gamedb probe: %s = %s\n", probe, entry ? entry->title.c_str() : "(not on record)");
    }
  }

  if (!Exists("bios"))
  {
    snprintf(g_loadError, sizeof(g_loadError),
             "error.no_bios");
    return false;
  }

  // The disc, if the project has one. A PlayStation with an empty tray is a
  // PlayStation sitting at its own menu, which is a machine worth booting.
  SystemBootParameters params;
  char name[512];
  g_discCount = std::min<int>(wbx_slot_count("disc"), MAX_DISCS);
  for (int i = 0; i < g_discCount; i++)
  {
    if (wbx_slot_name("disc", i, g_discs[i], sizeof(g_discs[i])) == nullptr)
    {
      g_discCount = i;
      break;
    }
  }
  if (g_discCount > 0)
  {
    // a project: every disc is mounted under its own name, extension and all
    params.filename = g_discs[0];
    g_discInserted = 0;
  }
  else if (Exists("disc"))
  {
    const char* const bare = NameTheBareFile();
    if (!bare && g_loadError[0] != '\0')
      return false;
    if (bare)
    {
      params.filename = bare;
      snprintf(g_discs[0], sizeof(g_discs[0]), "%s", bare);
      g_discCount = 1;
      g_discInserted = 0;
    }
  }

  // What a project put in its Save Data slot must be something this machine
  // reads. A saved game under a name nothing opens, booting to a blank card,
  // is worse than a project that will not boot.
  {
    char entry[512];
    const int saves = wbx_slot_count("savedata");
    for (int i = 0; i < saves; i++)
    {
      if (wbx_slot_name("savedata", i, entry, sizeof(entry)) == nullptr)
        break;
      bool known = false;
      for (int c = 0; c < CARDS; c++)
        known = known || (strcmp(entry, kCardNames[c]) == 0);
      if (!known)
      {
        snprintf(g_loadError, sizeof(g_loadError),
                 "error.save_name" TEXT_ARG "%s",
                 entry);
        return false;
      }
    }
  }

  // An empty tray gives "auto" nothing to read, and upstream then makes a
  // North American console of it whatever ROM is inside. Upstream can: it
  // picks the ROM to suit the region. Here the project picked the ROM, and a
  // European one puts the video chip in PAL mode on a console clocked for
  // NTSC - 314 lines off the wrong crystal, a field rate that is neither's,
  // and a frame rate declared to the frontend that the machine does not keep.
  // The ROM is the console it was made for, so with no disc the region is
  // the ROM's.
  if (g_settings.region == ConsoleRegion::Auto && g_discCount == 0)
  {
    const std::optional<BIOS::Image> rom = BIOS::LoadImageFromFile("bios", nullptr);
    if (rom.has_value() && rom->info && rom->info->region != ConsoleRegion::Auto)
      g_settings.region = rom->info->region;
  }

  params.override_fullscreen = false;
  params.override_start_paused = false;

  if (!System::BootSystem(std::move(params), error))
  {
    Fail("error.would_not_boot", *error);
    return false;
  }

  for (int c = 0; c < CARDS; c++)
  {
    if (!SeedCard(c))
      return false;
  }

  // every axis where it rests: a stick centred, a gun at the middle of the
  // picture, a mouse still
  for (int port = 0; port < PORTS; port++)
  {
    std::memcpy(g_axis[port], kAxisRest, sizeof(kAxisRest));
    Controller* const c = Pad::GetController(static_cast<u32>(port));
    if (c && (c->GetType() == ControllerType::GunCon || c->GetType() == ControllerType::Justifier))
      SetGunAim(c, kAxisRest[A_GUN_X], kAxisRest[A_GUN_Y]);
  }
  g_axisSaid = true;

  return true;
}

// The console's own controls, before the frame they are held for. Reset is
// the button on the case: pressed, the machine starts again with whatever is
// in the drive and on the cards.
void MachineBeforeFrame()
{
  // A mouse's movement is so much per frame, and upstream adds what it is
  // told to what the console has not yet asked for: told once a frame.
  for (int port = 0; port < PORTS; port++)
  {
    Controller* const c = Pad::GetController(static_cast<u32>(port));
    if (!c || c->GetType() != ControllerType::PlayStationMouse)
      continue;
    if (g_axis[port][A_MOUSE_X] != 0)
      c->SetBindState(2u, static_cast<float>(g_axis[port][A_MOUSE_X])); // Binding::PointerX
    if (g_axis[port][A_MOUSE_Y] != 0)
      c->SetBindState(3u, static_cast<float>(g_axis[port][A_MOUSE_Y])); // Binding::PointerY
  }

  DiscControls();
  if (g_console[0] && !g_consoleWas[0])
    System::ResetSystem();
  std::memcpy(g_consoleWas, g_console, sizeof(g_console));
}

void MachineAfterFrame()
{
  g_driveLight = g_driveLightNow;
}

} // namespace

// ---- input ----------------------------------------------------------------------

static void SetButton(int32_t index, int32_t state)
{
  if (index >= WIRE_CONSOLE && index < WIRE_COUNT)
  {
    g_console[index - WIRE_CONSOLE] = state ? 1 : 0;
    return;
  }
  if (index < 0 || index >= PORTS * PORT_BUTTONS)
    return;

  const int port = index / PORT_BUTTONS;
  const int wire = index % PORT_BUTTONS;
  const u8 held = state ? 1 : 0;
  const u8 was = g_held[port][wire];
  g_held[port][wire] = held;

  Controller* const c = Pad::GetController(static_cast<u32>(port));
  if (!c)
    return;
  const int bind = BindOf(c->GetType(), wire);
  if (bind < 0)
    return;

  if (wire == WIRE_ANALOG)
  {
    // The Analog button is a toggle on the pad itself, and upstream toggles
    // every time it is TOLD the button is down: it is told once per press.
    if (held && !was)
      c->SetBindState(static_cast<u32>(bind), 1.0f);
    return;
  }

  c->SetBindState(static_cast<u32>(bind), held ? 1.0f : 0.0f);
}

/* Which of the declared wires the machine can see: the frontend greys out the
 * rest. What a socket holds is decided at boot (a setting, and upstream's game
 * database on top of it), so the controller that is there is asked. */
static int IsButtonActive(int32_t index)
{
  // Reset always; the lid when there is a disc to put under it; the selector
  // when there is more than one to choose from
  if (index == WIRE_RESET)
    return 1;
  if (index == WIRE_DISC_SWAP)
    return g_discCount > 0 ? 1 : 0;
  if (index == WIRE_PREV_DISC || index == WIRE_NEXT_DISC)
    return g_discCount > 1 ? 1 : 0;
  if (index < 0 || index >= PORTS * PORT_BUTTONS)
    return 0;

  const Controller* const c = Pad::GetController(static_cast<u32>(index / PORT_BUTTONS));
  return (c && BindOf(c->GetType(), index % PORT_BUTTONS) >= 0) ? 1 : 0;
}

static int IsAxisActive(int32_t index)
{
  if (index < 0 || index >= PORTS * PORT_AXES)
    return 0;

  const Controller* const c = Pad::GetController(static_cast<u32>(index / PORT_AXES));
  return (c && HasAxis(c->GetType(), index % PORT_AXES)) ? 1 : 0;
}

static void SetAxis(int32_t index, int32_t value)
{
  if (index < 0 || index >= PORTS * PORT_AXES)
    return;

  // every axis starts where it rests, not at zero
  if (!g_axisSaid)
  {
    for (int p = 0; p < PORTS; p++)
      std::memcpy(g_axis[p], kAxisRest, sizeof(kAxisRest));
    g_axisSaid = true;
  }

  const int port = index / PORT_AXES;
  const int axis = index % PORT_AXES;
  Controller* const c = Pad::GetController(static_cast<u32>(port));
  if (!c || !HasAxis(c->GetType(), axis))
    return;

  switch (axis)
  {
    case A_LX:
    case A_LY:
    case A_RX:
    case A_RY:
    {
      const s32 byte = std::clamp<int32_t>(value, 0, 255);
      if (g_axis[port][axis] == byte)
        return;
      g_axis[port][axis] = byte;
      // HalfAxis: LLeft LRight LDown LUp RLeft RRight RDown RUp. A stick byte
      // grows to the right and downwards.
      static constexpr u8 kHalves[4][2] = {{1, 0}, {2, 3}, {5, 4}, {6, 7}};
      SetStickByte(c, kHalves[axis][0], kHalves[axis][1], static_cast<u8>(byte));
      break;
    }

    case A_TWIST:
    {
      const s32 byte = std::clamp<int32_t>(value, 0, 255);
      g_axis[port][axis] = byte;
      SetTwistByte(c, static_cast<u8>(byte));
      break;
    }

    case A_I:
    case A_II:
    case A_L:
    {
      // an analog button, as the byte it reports: bind 10, 11, 12 (after the
      // eight buttons and the two halves of the twist)
      const s32 byte = std::clamp<int32_t>(value, 0, 255);
      g_axis[port][axis] = byte;
      c->SetBindState(10u + static_cast<u32>(axis - A_I), static_cast<float>(byte) / 255.0f);
      break;
    }

    case A_GUN_X:
    case A_GUN_Y:
      g_axis[port][axis] = std::clamp<int32_t>(value, 0, 65535);
      SetGunAim(c, g_axis[port][A_GUN_X], g_axis[port][A_GUN_Y]);
      break;

    default:
      // the mouse: so much this frame, given to the pad once (MachineBeforeFrame)
      g_axis[port][axis] = std::clamp<int32_t>(value, -127, 127);
      break;
  }
}

// ---- save data: the memory cards ----------------------------------------------

static int g_saveList[CARDS];
static int g_saveCount;

static int32_t GetSaveDataFileCount(void)
{
  g_saveCount = 0;
  for (int c = 0; c < CARDS; c++)
  {
    if (g_booted && Pad::GetMemoryCard(static_cast<u32>(c)))
      g_saveList[g_saveCount++] = c;
  }
  return g_saveCount;
}

static const char* GetSaveDataFileName(int32_t i)
{
  return (i >= 0 && i < g_saveCount) ? kCardNames[g_saveList[i]] : "";
}

static int64_t GetSaveDataFileSize(int32_t i)
{
  return (i >= 0 && i < g_saveCount) ? static_cast<int64_t>(MemoryCardImage::DATA_SIZE) : 0;
}

static const uint8_t* GetSaveDataFileBuffer(int32_t i)
{
  if (i < 0 || i >= g_saveCount)
    return nullptr;
  MemoryCard* const card = Pad::GetMemoryCard(static_cast<u32>(g_saveList[i]));
  return card ? card->GetData().data() : nullptr;
}

// ---- the drive ------------------------------------------------------------------

/* One drive, when the project put a disc in it. Lit on a frame the console
 * read a sector. Which disc it holds and which the selector is on are told
 * apart, because the console tells them apart: between Next Disc and the lid
 * closing, the drive holds one disc - or none - and the selector is on
 * another. */
static int32_t GetDriveCount(void)
{
  return g_discCount > 0 ? 1 : 0;
}
static const char* GetDriveName(int32_t i)
{
  return "CD";
}
static int32_t GetDriveLight(int32_t i)
{
  return g_driveLight;
}
static int32_t GetDriveMediaCount(int32_t i)
{
  return g_discCount;
}
static const char* GetDriveMediaName(int32_t i, int32_t n)
{
  return (n >= 0 && n < g_discCount) ? g_discs[n] : "";
}
static int32_t GetDriveMediaSelected(int32_t i)
{
  return g_discSelected;
}
static int32_t GetDriveMediaInserted(int32_t i)
{
  return g_discInserted;
}
