// Wii Remote + Nunchuk emulation: WPAD/KPAD on top of SDL3.
//
// One remote with a Nunchuk attached is emulated on channel 0; channels 1-3
// report "no controller". KPAD semantics follow the RVL SDK's KPAD.c
// (src/RVL_SDK/kpad): samples are produced at the remote's 200 Hz report rate,
// KPADRead returns every sample since the previous read (newest first), and
// buttons/trigger/release/repeat are evaluated once per read and shared by all
// returned samples.
//
// Default controls
//   Pointer ........ mouse over the game image, or gamepad right stick
//   A .............. left mouse / Space / gamepad South
//   B (trigger) .... right mouse / Left Shift / gamepad East, right shoulder/trigger
//   Z (Nunchuk) .... Left Ctrl / Q / gamepad left shoulder/trigger
//   C (Nunchuk) .... E / gamepad North
//   Shake (spin) ... F / middle mouse / gamepad West
//   Nunchuk stick .. WASD / gamepad left stick
//   D-pad .......... arrow keys / gamepad D-pad
//   - / + .......... Minus / Equals, Enter or Escape / gamepad Back / Start
//   1 / 2 .......... 1 / 2 / gamepad left / right stick click
//   HOME ........... gamepad Guide (the HOME Menu itself isn't available)
//   Remote tilt .... I/K (pitch), J/L (roll); V toggles holding the remote upright

#include "port/input.hpp"

#include "os/scheduler.hpp"
#include "port/log.hpp"
#include "port/savestate.hpp"

#include <revolution/kpad.h>
#include <revolution/wpad.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

extern "C" s32 WPADReadFaceData(s32 chan, void* buf, u32 len, u32 addr, WPADCallback callback);

namespace {

constexpr u64 kSamplePeriodNs = 5'000'000;  // Wii Remote report rate: 200 Hz
constexpr u32 kMaxSamples = 120;            // KPAD ring buffer size
constexpr u64 kShakeDurationNs = 80'000'000;
constexpr u64 kShakeHalfPeriodNs = 20'000'000;
constexpr float kRemoteAccMax = 3.4f;  // KPAD.c kp_rm_acc_max
constexpr float kFsAccMax = 2.1f;      // KPAD.c kp_fs_acc_max
constexpr float kTiltMax = 0.61f;      // ~35 degrees
constexpr float kStickDeadzone = 0.15f;
constexpr float kPointerStickSpeed = 1.4f;  // KPAD units per second
constexpr u32 kConnectDelayFrames = 30;

struct Vec3f {
    float x, y, z;
};

struct State {
    std::mutex mutex;

    // Live input, updated on the main thread.
    u32 buttons = 0;
    float stickX = 0.0f;
    float stickY = 0.0f;
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    bool pointerValid = false;
    bool pointerFromGamepad = false;
    float pitch = 0.0f;  // tilt relative to the baseline, radians
    float roll = 0.0f;
    bool upright = false;
    u64 shakeStartNs = 0;
    u64 lastFrameNs = 0;
    u32 frame = 0;

    float viewX = 0.0f, viewY = 0.0f, viewW = 0.0f, viewH = 0.0f;

    // Edge detection for keys/buttons that trigger actions.
    bool shakeHeld = false;
    bool uprightHeld = false;

    // KPAD read state (channel 0).
    u64 lastReadNs = 0;
    u32 hold = 0;
    u16 repeatTime = 0;
    u16 repeatNext = KPAD_BTN_NO_RPT_DELAY;
    u16 repeatDelay = KPAD_BTN_NO_RPT_DELAY;
    u16 repeatPulse = 0;
    float prevPointerX = 0.0f;
    float prevPointerY = 0.0f;
    float prevAccValue = 1.0f;
    float prevFsAccValue = 1.0f;
    float prevHoriX = 1.0f;
    float prevHoriY = 0.0f;

    // WPAD bookkeeping.
    WPADConnectCallback connectCb[WPAD_MAX_CONTROLLERS] = {};
    WPADExtensionCallback extensionCb[WPAD_MAX_CONTROLLERS] = {};
    WPADAlloc allocFn = nullptr;
    WPADFree freeFn = nullptr;
    u32 connectRegisteredFrame = 0;
    bool connectPending = false;
    bool connected = false;
    bool extensionPending = false;
    bool motor = false;
    bool motorApplied = false;
};

// Scripted input for automated runs: SMG_INPUT_SCRIPT="frame:TOKENS;..."
// where TOKENS is '+'-separated: A B Z C PLUS MINUS ONE TWO UP DOWN LEFT
// RIGHT HOME SHAKE PX<x> PY<y> (pointer, -1..1) SX<x> SY<y> (stick, -1..1).
// Each entry holds from its frame until the next entry; an empty entry
// releases everything. AUTOA taps A whenever the game shows its "press A"
// prompt (message windows), and only then. SAVE<n>/LOAD<n> save or load save
// state slot n when the entry is reached.
struct ScriptStep {
    u32 frame = 0;
    u32 buttons = 0;
    bool shake = false;
    bool autoA = false;
    int saveSlot = 0;
    int loadSlot = 0;
    bool pointer = false;
    float px = 0.0f, py = 0.0f;
    float sx = 0.0f, sy = 0.0f;
};

std::vector<ScriptStep> parseScript(const char* text) {
    std::vector<ScriptStep> steps;
    std::string str = text;
    size_t pos = 0;
    while (pos < str.size()) {
        size_t end = str.find(';', pos);
        if (end == std::string::npos) {
            end = str.size();
        }
        const std::string item = str.substr(pos, end - pos);
        pos = end + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        ScriptStep step;
        step.frame = static_cast<u32>(strtoul(item.c_str(), nullptr, 10));
        std::string toks = item.substr(colon + 1);
        size_t tp = 0;
        while (tp < toks.size()) {
            size_t te = toks.find('+', tp);
            if (te == std::string::npos) {
                te = toks.size();
            }
            const std::string t = toks.substr(tp, te - tp);
            tp = te + 1;
            static const std::pair<const char*, u32> kButtons[] = {
                {"A", WPAD_BUTTON_A},       {"B", WPAD_BUTTON_B},         {"Z", WPAD_BUTTON_Z},
                {"C", WPAD_BUTTON_C},       {"PLUS", WPAD_BUTTON_PLUS},   {"MINUS", WPAD_BUTTON_MINUS},
                {"ONE", WPAD_BUTTON_1},     {"TWO", WPAD_BUTTON_2},       {"UP", WPAD_BUTTON_UP},
                {"DOWN", WPAD_BUTTON_DOWN}, {"LEFT", WPAD_BUTTON_LEFT},   {"RIGHT", WPAD_BUTTON_RIGHT},
                {"HOME", WPAD_BUTTON_HOME},
            };
            bool known = false;
            for (const auto& [name, bit] : kButtons) {
                if (t == name) {
                    step.buttons |= bit;
                    known = true;
                }
            }
            if (known || t.empty()) {
                continue;
            }
            if (t == "SHAKE") {
                step.shake = true;
            } else if (t == "AUTOA") {
                step.autoA = true;
            } else if (t.size() == 5 && t.compare(0, 4, "SAVE") == 0) {
                step.saveSlot = t[4] - '0';
            } else if (t.size() == 5 && t.compare(0, 4, "LOAD") == 0) {
                step.loadSlot = t[4] - '0';
            } else if (t.size() > 2 && (t[0] == 'P' || t[0] == 'S') && (t[1] == 'X' || t[1] == 'Y')) {
                const float v = strtof(t.c_str() + 2, nullptr);
                if (t[0] == 'P') {
                    step.pointer = true;
                    (t[1] == 'X' ? step.px : step.py) = v;
                } else {
                    (t[1] == 'X' ? step.sx : step.sy) = v;
                }
            } else {
                PORT_WARN("input", "unknown script token '{}'", t);
            }
        }
        steps.push_back(step);
    }
    return steps;
}

std::atomic<u32> sPromptAFrame{0};

const ScriptStep* currentScriptStep(u32 frame) {
    static const std::vector<ScriptStep> sSteps = [] {
        const char* text = getenv("SMG_INPUT_SCRIPT");
        return text != nullptr ? parseScript(text) : std::vector<ScriptStep>{};
    }();
    const ScriptStep* current = nullptr;
    for (const auto& step : sSteps) {
        if (step.frame <= frame) {
            current = &step;
        }
    }
    return current;
}

bool scriptActive() {
    static const bool sActive = getenv("SMG_INPUT_SCRIPT") != nullptr;
    return sActive;
}

// Input recording (SMG_INPUT_RECORD=<file>): writes the inputs as an
// SMG_INPUT_SCRIPT timeline, one entry per change, to replay a session.
void recordFrame(const State& s, bool shake) {
    static FILE* sFile = [] {
        const char* path = getenv("SMG_INPUT_RECORD");
        return path != nullptr && *path != '\0' ? fopen(path, "w") : nullptr;
    }();
    if (sFile == nullptr) {
        return;
    }
    static const std::pair<const char*, u32> kButtons[] = {
        {"A", WPAD_BUTTON_A},       {"B", WPAD_BUTTON_B},       {"Z", WPAD_BUTTON_Z},       {"C", WPAD_BUTTON_C},
        {"PLUS", WPAD_BUTTON_PLUS}, {"MINUS", WPAD_BUTTON_MINUS}, {"ONE", WPAD_BUTTON_1},    {"TWO", WPAD_BUTTON_2},
        {"UP", WPAD_BUTTON_UP},     {"DOWN", WPAD_BUTTON_DOWN}, {"LEFT", WPAD_BUTTON_LEFT}, {"RIGHT", WPAD_BUTTON_RIGHT},
        {"HOME", WPAD_BUTTON_HOME},
    };
    std::string tokens;
    auto add = [&](const std::string& t) {
        if (!tokens.empty()) {
            tokens += '+';
        }
        tokens += t;
    };
    for (const auto& [name, bit] : kButtons) {
        if (s.buttons & bit) {
            add(name);
        }
    }
    if (shake) {
        add("SHAKE");
    }
    auto quant = [](float v, float step) { return std::round(v / step) * step; };
    char tmp[32];
    if (s.stickX != 0.0f || s.stickY != 0.0f) {
        snprintf(tmp, sizeof(tmp), "SX%.2f", quant(s.stickX, 0.05f));
        add(tmp);
        snprintf(tmp, sizeof(tmp), "SY%.2f", quant(s.stickY, 0.05f));
        add(tmp);
    }
    if (s.pointerValid) {
        snprintf(tmp, sizeof(tmp), "PX%.2f", quant(s.pointerX, 0.02f));
        add(tmp);
        snprintf(tmp, sizeof(tmp), "PY%.2f", quant(s.pointerY, 0.02f));
        add(tmp);
    }
    static std::string sLast = "\x01";
    if (tokens != sLast) {
        sLast = tokens;
        fprintf(sFile, "%u:%s;\n", s.frame, tokens.c_str());
        fflush(sFile);
    }
}

State& state() {
    static State s;
    return s;
}

std::vector<SDL_Gamepad*>& gamepads() {
    static std::vector<SDL_Gamepad*> pads;
    return pads;
}

float clampf(float v, float lo, float hi) { return std::min(std::max(v, lo), hi); }

float applyDeadzone(float v) {
    const float a = std::fabs(v);
    if (a < kStickDeadzone) {
        return 0.0f;
    }
    return std::copysign((a - kStickDeadzone) / (1.0f - kStickDeadzone), v);
}

float axis(SDL_Gamepad* pad, SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad, a) / 32767.0f; }

// Gravity as seen by the remote's accelerometer in KPAD space (x right,
// y = -1 when lying flat face up, z along the pointing direction).
Vec3f remoteGravity(const State& s) {
    const float p = (s.upright ? 1.5707964f : 0.0f) + s.pitch;
    const float r = s.roll;
    return {std::sin(r), -std::cos(r) * std::cos(p), std::cos(r) * std::sin(p)};
}

// Extra acceleration of a shake gesture at time t, or false outside of one.
bool shakeOffset(const State& s, u64 t, float& sign) {
    if (s.shakeStartNs == 0 || t < s.shakeStartNs || t - s.shakeStartNs >= kShakeDurationNs) {
        return false;
    }
    sign = ((t - s.shakeStartNs) / kShakeHalfPeriodNs) % 2 == 0 ? 1.0f : -1.0f;
    return true;
}

Vec3f clampVec(Vec3f v, float m) { return {clampf(v.x, -m, m), clampf(v.y, -m, m), clampf(v.z, -m, m)}; }

float length(Vec3f v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

void calcButtonRepeat(State& s, KPADStatus& st, u32 count) {
    if (st.trig != 0 || st.release != 0) {
        s.repeatTime = 0;
        s.repeatNext = s.repeatDelay;
        if (st.trig && s.repeatPulse) {
            st.hold |= KPAD_BUTTON_RPT;
        }
    } else if (st.hold != 0) {
        u32 time = s.repeatTime + count;
        if (time >= KPAD_BTN_NO_RPT_DELAY) {
            time -= KPAD_BTN_NO_RPT_DELAY;
        }
        s.repeatTime = (u16)time;
        if (s.repeatTime >= s.repeatNext) {
            st.hold |= KPAD_BUTTON_RPT;
            s.repeatNext += s.repeatPulse;
            if (s.repeatTime >= KPAD_BTN_RPT_TIME_MAX) {
                s.repeatTime -= KPAD_BTN_RPT_TIME_MAX;
                s.repeatNext -= KPAD_BTN_RPT_TIME_MAX;
            }
        }
    }
}

void readDevices(State& s, u64 now) {
    const float dt = s.lastFrameNs ? (float)(now - s.lastFrameNs) * 1e-9f : 0.0f;
    s.lastFrameNs = now;

    u32 buttons = 0;
    bool shake = false;
    float stickX = 0.0f, stickY = 0.0f;
    float tiltPitch = 0.0f, tiltRoll = 0.0f;

    // Keyboard.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    auto key = [&](SDL_Scancode sc) { return keys != nullptr && keys[sc]; };
    if (key(SDL_SCANCODE_SPACE)) buttons |= WPAD_BUTTON_A;
    if (key(SDL_SCANCODE_LSHIFT)) buttons |= WPAD_BUTTON_B;
    if (key(SDL_SCANCODE_LCTRL) || key(SDL_SCANCODE_Q)) buttons |= WPAD_BUTTON_Z;
    if (key(SDL_SCANCODE_E)) buttons |= WPAD_BUTTON_C;
    if (key(SDL_SCANCODE_MINUS)) buttons |= WPAD_BUTTON_MINUS;
    if (key(SDL_SCANCODE_EQUALS) || key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_ESCAPE)) buttons |= WPAD_BUTTON_PLUS;
    if (key(SDL_SCANCODE_1)) buttons |= WPAD_BUTTON_1;
    if (key(SDL_SCANCODE_2)) buttons |= WPAD_BUTTON_2;
    if (key(SDL_SCANCODE_UP)) buttons |= WPAD_BUTTON_UP;
    if (key(SDL_SCANCODE_DOWN)) buttons |= WPAD_BUTTON_DOWN;
    if (key(SDL_SCANCODE_LEFT)) buttons |= WPAD_BUTTON_LEFT;
    if (key(SDL_SCANCODE_RIGHT)) buttons |= WPAD_BUTTON_RIGHT;
    shake |= key(SDL_SCANCODE_F);
    stickX += (key(SDL_SCANCODE_D) ? 1.0f : 0.0f) - (key(SDL_SCANCODE_A) ? 1.0f : 0.0f);
    stickY += (key(SDL_SCANCODE_W) ? 1.0f : 0.0f) - (key(SDL_SCANCODE_S) ? 1.0f : 0.0f);
    tiltPitch += (key(SDL_SCANCODE_I) ? kTiltMax : 0.0f) - (key(SDL_SCANCODE_K) ? kTiltMax : 0.0f);
    tiltRoll += (key(SDL_SCANCODE_L) ? kTiltMax : 0.0f) - (key(SDL_SCANCODE_J) ? kTiltMax : 0.0f);
    const bool uprightKey = key(SDL_SCANCODE_V);

    // Mouse (ignored during scripted runs, which must not depend on where the
    // cursor happens to be).
    float mx = 0.0f, my = 0.0f;
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(&mx, &my);
    SDL_Window* focus = scriptActive() ? nullptr : SDL_GetMouseFocus();
    if (focus != nullptr) {
        if (mouse & SDL_BUTTON_LMASK) buttons |= WPAD_BUTTON_A;
        if (mouse & SDL_BUTTON_RMASK) buttons |= WPAD_BUTTON_B;
        if (mouse & SDL_BUTTON_MMASK) shake = true;
    }

    // Gamepads (all connected pads drive the one emulated remote).
    float rsX = 0.0f, rsY = 0.0f;
    for (SDL_Gamepad* pad : gamepads()) {
        auto btn = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
        if (btn(SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= WPAD_BUTTON_A;
        if (btn(SDL_GAMEPAD_BUTTON_EAST) || btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) buttons |= WPAD_BUTTON_B;
        if (btn(SDL_GAMEPAD_BUTTON_NORTH)) buttons |= WPAD_BUTTON_C;
        if (btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) buttons |= WPAD_BUTTON_Z;
        if (btn(SDL_GAMEPAD_BUTTON_BACK)) buttons |= WPAD_BUTTON_MINUS;
        if (btn(SDL_GAMEPAD_BUTTON_START)) buttons |= WPAD_BUTTON_PLUS;
        if (btn(SDL_GAMEPAD_BUTTON_GUIDE)) buttons |= WPAD_BUTTON_HOME;
        if (btn(SDL_GAMEPAD_BUTTON_LEFT_STICK)) buttons |= WPAD_BUTTON_1;
        if (btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) buttons |= WPAD_BUTTON_2;
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_UP)) buttons |= WPAD_BUTTON_UP;
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) buttons |= WPAD_BUTTON_DOWN;
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) buttons |= WPAD_BUTTON_LEFT;
        if (btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) buttons |= WPAD_BUTTON_RIGHT;
        if (btn(SDL_GAMEPAD_BUTTON_WEST)) shake = true;
        if (axis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f) buttons |= WPAD_BUTTON_Z;
        if (axis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f) buttons |= WPAD_BUTTON_B;
        stickX += applyDeadzone(axis(pad, SDL_GAMEPAD_AXIS_LEFTX));
        stickY -= applyDeadzone(axis(pad, SDL_GAMEPAD_AXIS_LEFTY));
        rsX += applyDeadzone(axis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
        rsY += applyDeadzone(axis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
    }

    const ScriptStep* script = currentScriptStep(s.frame);
    if (script != nullptr) {
        static const ScriptStep* sLastStep = nullptr;
        if (script != sLastStep) {
            sLastStep = script;
            if (script->saveSlot != 0) {
                port::savestate::requestSave(script->saveSlot);
            }
            if (script->loadSlot != 0) {
                port::savestate::requestLoad(script->loadSlot);
            }
        }
        if (script->autoA) {
            // Tap A (6 frames down, then at least 24 up) while a prompt shows.
            static u32 sLastTap = 0;
            const u32 prompt = sPromptAFrame.load(std::memory_order_relaxed);
            if (s.frame - sLastTap < 6) {
                buttons |= WPAD_BUTTON_A;
            } else if (prompt != 0 && s.frame - prompt <= 2 && s.frame - sLastTap >= 30) {
                sLastTap = s.frame;
                buttons |= WPAD_BUTTON_A;
            }
        }
        buttons |= script->buttons;
        shake |= script->shake;
        stickX += script->sx;
        stickY += script->sy;
    }

    // Nunchuk stick: clamp to the unit circle.
    const float len = std::sqrt(stickX * stickX + stickY * stickY);
    if (len > 1.0f) {
        stickX /= len;
        stickY /= len;
    }
    s.stickX = stickX;
    s.stickY = stickY;

    // Pointer: the gamepad's right stick takes over until the mouse moves.
    if (rsX != 0.0f || rsY != 0.0f) {
        if (!s.pointerFromGamepad) {
            s.pointerFromGamepad = true;
            if (!s.pointerValid) {
                s.pointerX = s.pointerY = 0.0f;
            }
        }
        s.pointerX = clampf(s.pointerX + rsX * kPointerStickSpeed * dt, -1.0f, 1.0f);
        s.pointerY = clampf(s.pointerY + rsY * kPointerStickSpeed * dt, -1.0f, 1.0f);
        s.pointerValid = true;
    } else if (!s.pointerFromGamepad) {
        s.pointerValid = false;
        if (focus != nullptr) {
            int ww = 0, wh = 0;
            SDL_GetWindowSize(focus, &ww, &wh);
            float vx = s.viewX, vy = s.viewY, vw = s.viewW, vh = s.viewH;
            if (vw <= 0.0f || vh <= 0.0f) {
                vx = vy = 0.0f;
                vw = (float)ww;
                vh = (float)wh;
            }
            if (vw > 0.0f && vh > 0.0f && mx >= vx && my >= vy && mx < vx + vw && my < vy + vh) {
                s.pointerX = (mx - vx) / vw * 2.0f - 1.0f;
                s.pointerY = (my - vy) / vh * 2.0f - 1.0f;
                s.pointerValid = true;
            }
        }
    }

    if (script != nullptr && script->pointer) {
        s.pointerX = script->px;
        s.pointerY = script->py;
        s.pointerValid = true;
    }

    // Tilt eases towards the requested angle.
    const float ease = dt > 0.0f ? std::min(1.0f, dt * 12.0f) : 1.0f;
    s.pitch += (tiltPitch - s.pitch) * ease;
    s.roll += (tiltRoll - s.roll) * ease;
    if (uprightKey && !s.uprightHeld) {
        s.upright = !s.upright;
        PORT_INFO("input", "remote held {}", s.upright ? "upright" : "flat");
    }
    s.uprightHeld = uprightKey;

    // A shake starts on the press edge (holding the key doesn't repeat it).
    if (shake && !s.shakeHeld && (s.shakeStartNs == 0 || now - s.shakeStartNs >= kShakeDurationNs)) {
        s.shakeStartNs = now;
    }
    s.shakeHeld = shake;

    s.buttons = buttons;
}

void fillSample(State& s, KPADStatus& st, u64 t, float vecX, float vecY) {
    const u32 hold = st.hold;
    const u32 trig = st.trig;
    const u32 release = st.release;
    memset(&st, 0, sizeof(st));
    st.hold = hold;
    st.trig = trig;
    st.release = release;

    // Remote accelerometer.
    Vec3f acc = remoteGravity(s);
    Vec3f fsAcc = {0.0f, -1.0f, 0.0f};
    float sign = 1.0f;
    if (shakeOffset(s, t, sign)) {
        acc = clampVec({acc.x + sign * 2.2f, acc.y + 2.2f, acc.z}, kRemoteAccMax);
        fsAcc = clampVec({fsAcc.x + sign * 2.1f, fsAcc.y + 2.1f, fsAcc.z}, kFsAccMax);
    }
    st.acc = {acc.x, acc.y, acc.z};
    st.acc_value = length(acc);
    st.acc_speed = st.acc_value - s.prevAccValue;
    s.prevAccValue = st.acc_value;

    const float vlen = std::sqrt(acc.y * acc.y + acc.z * acc.z);
    st.acc_vertical = vlen > 0.0f ? Vec2{-acc.y / vlen, acc.z / vlen} : Vec2{1.0f, 0.0f};

    // Pointer (DPD). dpd_valid_fg == 2: both sensor bar dots are visible.
    st.pos = {s.pointerX, s.pointerY};
    st.vec = {vecX, vecY};
    st.speed = std::sqrt(vecX * vecX + vecY * vecY);
    st.horizon = {std::cos(s.roll), std::sin(s.roll)};
    st.hori_vec = {st.horizon.x - s.prevHoriX, st.horizon.y - s.prevHoriY};
    st.hori_speed = std::sqrt(st.hori_vec.x * st.hori_vec.x + st.hori_vec.y * st.hori_vec.y);
    s.prevHoriX = st.horizon.x;
    s.prevHoriY = st.horizon.y;
    st.dist = 2.0f;
    st.dist_vec = 0.0f;
    st.dist_speed = 0.0f;
    st.dpd_valid_fg = s.pointerValid ? 2 : 0;

    st.dev_type = WPAD_DEV_FREESTYLE;
    st.wpad_err = WPAD_ERR_NONE;
    st.data_format = WPAD_FMT_FREESTYLE_ACC_DPD;

    // Nunchuk.
    st.ex_status.fs.stick = {s.stickX, s.stickY};
    st.ex_status.fs.acc = {fsAcc.x, fsAcc.y, fsAcc.z};
    st.ex_status.fs.acc_value = length(fsAcc);
    st.ex_status.fs.acc_speed = st.ex_status.fs.acc_value - s.prevFsAccValue;
    s.prevFsAccValue = st.ex_status.fs.acc_value;
}

}  // namespace

namespace port::input {

void handleEvent(const SDL_Event& event) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED: {
        SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which);
        if (pad != nullptr) {
            gamepads().push_back(pad);
            PORT_INFO("input", "gamepad connected: {}", SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "?");
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        auto& pads = gamepads();
        for (auto it = pads.begin(); it != pads.end(); ++it) {
            if (SDL_GetGamepadID(*it) == event.gdevice.which) {
                SDL_CloseGamepad(*it);
                pads.erase(it);
                break;
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        s.pointerFromGamepad = false;
        break;
    default:
        break;
    }
}

void beginFrame() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    const u64 now = SDL_GetTicksNS();
    readDevices(s, now);
    s.frame++;
    recordFrame(s, s.shakeHeld);

    // Rumble: applied here because SDL gamepad calls belong on the main thread.
    if (s.motor != s.motorApplied) {
        s.motorApplied = s.motor;
        for (SDL_Gamepad* pad : gamepads()) {
            SDL_RumbleGamepad(pad, s.motor ? 0x9000 : 0, s.motor ? 0x9000 : 0, s.motor ? 10000 : 0);
        }
    }

    // A real remote connects some time after the callbacks are installed;
    // announce the connection, then the attached Nunchuk.
    if (s.connectPending && s.connectCb[0] != nullptr && s.frame - s.connectRegisteredFrame >= kConnectDelayFrames) {
        s.connectPending = false;
        s.connected = true;
        s.extensionPending = true;
        WPADConnectCallback cb = s.connectCb[0];
        port::os::postInterrupt([cb] { cb(WPAD_CHAN0, WPAD_ERR_NONE); });
    } else if (s.extensionPending && s.connected && s.extensionCb[0] != nullptr) {
        s.extensionPending = false;
        WPADExtensionCallback cb = s.extensionCb[0];
        port::os::postInterrupt([cb] { cb(WPAD_CHAN0, WPAD_DEV_FREESTYLE); });
    }
}

void notifyPromptA() {
    sPromptAFrame.store(state().frame, std::memory_order_relaxed);
}

void setPointerViewport(float x, float y, float width, float height) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.viewX = x;
    s.viewY = y;
    s.viewW = width;
    s.viewH = height;
}

}  // namespace port::input

// ---------------------------------------------------------------------------
// KPAD
// ---------------------------------------------------------------------------

extern "C" {

void KPADInit() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.lastReadNs = 0;
    s.hold = 0;
    s.repeatDelay = KPAD_BTN_NO_RPT_DELAY;
    s.repeatPulse = 0;
    s.repeatTime = 0;
    s.repeatNext = s.repeatDelay;
}

void KPADReset(void) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.lastReadNs = 0;
    s.hold = 0;
    s.repeatTime = 0;
    s.repeatNext = s.repeatDelay;
}

void KPADSetBtnRepeat(s32 chan, f32 delay_sec, f32 pulse_sec) {
    if (chan != WPAD_CHAN0) {
        return;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (pulse_sec != 0.0f) {
        s.repeatDelay = (u16)(s32)(delay_sec * 200.0f + 0.5f);
        s.repeatPulse = (u16)(s32)(pulse_sec * 200.0f + 0.5f);
    } else {
        s.repeatDelay = KPAD_BTN_NO_RPT_DELAY;
        s.repeatPulse = 0;
    }
    s.repeatTime = 0;
    s.repeatNext = s.repeatDelay;
}

// Sensor-bar/pointer tuning parameters only affect KPAD's filtering of real
// IR data; the emulated pointer is exact.
void KPADSetSensorHeight(s32, f32) {}
void KPADSetAccParam(s32, f32, f32) {}
void KPADSetPosParam(s32, f32, f32) {}
void KPADSetHoriParam(s32, f32, f32) {}
void KPADSetDistParam(s32, f32, f32) {}

s32 KPADRead(s32 chan, KPADStatus samplingBufs[], u32 length) {
    if (chan != WPAD_CHAN0 || samplingBufs == nullptr || length == 0) {
        return 0;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (!s.connected) {
        return 0;
    }

    const u64 now = SDL_GetTicksNS();
    u32 count = 1;
    if (s.lastReadNs != 0 && now > s.lastReadNs) {
        const u64 elapsed = now - s.lastReadNs;
        count = (u32)std::min<u64>(std::max<u64>(elapsed / kSamplePeriodNs, 1), kMaxSamples);
        // Keep the 200 Hz phase unless the game stalled for a long time.
        s.lastReadNs = elapsed > kMaxSamples * kSamplePeriodNs ? now : s.lastReadNs + count * kSamplePeriodNs;
    } else {
        s.lastReadNs = now;
    }
    count = std::min(count, length);

    // Buttons are evaluated once per read (KPAD.c read_kpad_button).
    KPADStatus head;
    memset(&head, 0, sizeof(head));
    const u32 oldHold = s.hold & KPAD_BUTTON_MASK;
    const u32 newHold = s.buttons & KPAD_BUTTON_MASK;
    const u32 change = newHold ^ oldHold;
    head.hold = newHold;
    head.trig = change & newHold;
    head.release = change & oldHold;
    calcButtonRepeat(s, head, count);
    s.hold = head.hold;

    const float vecX = (s.pointerX - s.prevPointerX) / (float)count;
    const float vecY = (s.pointerY - s.prevPointerY) / (float)count;
    s.prevPointerX = s.pointerX;
    s.prevPointerY = s.pointerY;

    // Oldest sample last: index 0 is the newest.
    for (s32 i = (s32)count - 1; i >= 0; i--) {
        KPADStatus& st = samplingBufs[i];
        st.hold = head.hold;
        st.trig = head.trig;
        st.release = head.release;
        fillSample(s, st, now - (u64)i * kSamplePeriodNs, vecX, vecY);
    }
    return (s32)count;
}

// ---------------------------------------------------------------------------
// WPAD
// ---------------------------------------------------------------------------

void WPADRegisterAllocator(WPADAlloc alloc, WPADFree free) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.allocFn = alloc;
    s.freeFn = free;
}

// The real library carves its Bluetooth buffers from this; the emulation needs
// none, but the game sizes a heap from it.
u32 WPADGetWorkMemorySize(void) { return 0x4000; }

void WPADDisconnect(s32 chan) {
    if (chan == WPAD_CHAN0) {
        PORT_DEBUG("input", "ignoring WPADDisconnect on the emulated remote");
    }
}

s32 WPADProbe(s32 chan, u32* type) {
    const bool present = chan == WPAD_CHAN0 && state().connected;
    if (type != nullptr) {
        *type = present ? WPAD_DEV_FREESTYLE : WPAD_DEV_NOT_FOUND;
    }
    return present ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
}

s32 WPADGetInfoAsync(s32 chan, WPADInfo* info, WPADCallback callback) {
    if (chan != WPAD_CHAN0 || !state().connected) {
        return WPAD_ERR_NO_CONTROLLER;
    }
    if (info != nullptr) {
        info->dpd = TRUE;
        info->speaker = FALSE;
        info->attach = TRUE;
        info->lowBat = FALSE;
        info->nearempty = FALSE;
        info->battery = WPAD_BATTERY_LEVEL_MAX;
        info->led = 1;
        info->protocol = 0;
        info->firmware = 0;
    }
    if (callback != nullptr) {
        port::os::postInterrupt([callback, chan] { callback(chan, WPAD_ERR_NONE); });
    }
    return WPAD_ERR_NONE;
}

// No Wii Remote speaker: accept commands but never start playback, which
// leaves SpkSpeakerCtrl idle.
BOOL WPADIsSpeakerEnabled(s32) { return FALSE; }
s32 WPADControlSpeaker(s32 chan, u32, WPADCallback) {
    return chan == WPAD_CHAN0 ? WPAD_ERR_NONE : WPAD_ERR_NO_CONTROLLER;
}
u8 WPADGetSpeakerVolume(void) { return 0; }
s32 WPADSendStreamData(s32, void*, u16) { return WPAD_ERR_NONE; }
BOOL WPADCanSendStreamData(s32) { return FALSE; }

void WPADControlMotor(s32 chan, u32 command) {
    if (chan != WPAD_CHAN0) {
        return;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.motor = command == WPAD_MOTOR_RUMBLE;
}

void WPADSetAutoSleepTime(u8) {}

u8 WPADGetSensorBarPosition(void) { return WPAD_SENSOR_BAR_POS_TOP; }

s32 WPADReadFaceData(s32, void*, u32, u32, WPADCallback) { return WPAD_ERR_NO_CONTROLLER; }

WPADConnectCallback WPADSetConnectCallback(s32 chan, WPADConnectCallback callback) {
    if (chan < 0 || chan >= WPAD_MAX_CONTROLLERS) {
        return nullptr;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    WPADConnectCallback prev = s.connectCb[chan];
    s.connectCb[chan] = callback;
    if (chan == WPAD_CHAN0 && callback != nullptr && !s.connected) {
        s.connectPending = true;
        s.connectRegisteredFrame = s.frame;
    }
    return prev;
}

WPADExtensionCallback WPADSetExtensionCallback(s32 chan, WPADExtensionCallback callback) {
    if (chan < 0 || chan >= WPAD_MAX_CONTROLLERS) {
        return nullptr;
    }
    State& s = state();
    std::lock_guard lock(s.mutex);
    WPADExtensionCallback prev = s.extensionCb[chan];
    s.extensionCb[chan] = callback;
    if (chan == WPAD_CHAN0 && callback != nullptr && s.connected) {
        s.extensionPending = true;
    }
    return prev;
}

}  // extern "C"

extern "C" void PortNotifyPromptA(void) { port::input::notifyPromptA(); }
