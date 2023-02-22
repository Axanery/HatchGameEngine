#if INTERFACE
#include <Engine/Includes/Standard.h>
#include <Engine/InputManager.h>
#include <Engine/Audio/AudioManager.h>
#include <Engine/Scene.h>
#include <Engine/Math/Math.h>
#include <Engine/TextFormats/INI/INI.h>
#include <Engine/TextFormats/XML/XMLParser.h>
#include <Engine/TextFormats/XML/XMLNode.h>

class Application {
public:
    static INI*        Settings;
    static char        SettingsFile[4096];

    static XMLNode*    GameConfig;

    static float       FPS;
    static bool        Running;
    static bool        GameStart;

    static SDL_Window* Window;
    static char        WindowTitle[256];
    static int         WindowWidth;
    static int         WindowHeight;
    static int         DefaultMonitor;
    static Platforms   Platform;

    static int         UpdatesPerFrame;
    static bool        Stepper;
    static bool        Step;

    static int         MasterVolume;
    static int         MusicVolume;
    static int         SoundVolume;
};
#endif

#include <Engine/Application.h>
#include <Engine/Graphics.h>

#include <Engine/Bytecode/BytecodeObjectManager.h>
#include <Engine/Bytecode/GarbageCollector.h>
#include <Engine/Bytecode/SourceFileMap.h>
#include <Engine/Diagnostics/Clock.h>
#include <Engine/Diagnostics/Log.h>
#include <Engine/Diagnostics/Memory.h>
#include <Engine/Diagnostics/MemoryPools.h>
#include <Engine/Filesystem/Directory.h>
#include <Engine/ResourceTypes/ResourceManager.h>
#include <Engine/TextFormats/XML/XMLParser.h>
#include <Engine/TextFormats/XML/XMLNode.h>
#include <Engine/Utilities/StringUtils.h>

#include <Engine/Media/MediaSource.h>
#include <Engine/Media/MediaPlayer.h>

#ifdef IOS
extern "C" {
    #include <Engine/Platforms/iOS/MediaPlayer.h>
}
#endif

#ifdef MSYS
    #include <windows.h>
#endif

#if   WIN32
    Platforms Application::Platform = Platforms::Windows;
#elif MACOSX
    Platforms Application::Platform = Platforms::MacOSX;
#elif LINUX
    Platforms Application::Platform = Platforms::Linux;
#elif SWITCH
    Platforms Application::Platform = Platforms::Switch;
#elif PLAYSTATION
    Platforms Application::Platform = Platforms::Playstation;
#elif XBOX
    Platforms Application::Platform = Platforms::Xbox;
#elif ANDROID
    Platforms Application::Platform = Platforms::Android;
#elif IOS
    Platforms Application::Platform = Platforms::iOS;
#else
    Platforms Application::Platform = Platforms::Unknown;
#endif

INI*        Application::Settings = NULL;
char        Application::SettingsFile[4096];

XMLNode*    Application::GameConfig = NULL;

float       Application::FPS = 60.f;
int         TargetFPS = 60;
bool        Application::Running = false;
bool        Application::GameStart = false;

SDL_Window* Application::Window = NULL;
char        Application::WindowTitle[256];
int         Application::WindowWidth = 848;
int         Application::WindowHeight = 480;
int         Application::DefaultMonitor = 0;

int         Application::UpdatesPerFrame = 1;
bool        Application::Stepper = false;
bool        Application::Step = false;

int         Application::MasterVolume = 100;
int         Application::MusicVolume = 100;
int         Application::SoundVolume = 100;

char    StartingScene[256];

bool    DevMenu = false;
bool    ShowFPS = false;
bool    TakeSnapshot = false;
bool    DoNothing = false;
int     UpdatesPerFastForward = 4;

int     BenchmarkFrameCount = 0;
double  BenchmarkTickStart = 0.0;

double  Overdelay = 0.0;
double  FrameTimeStart = 0.0;
double  FrameTimeDesired = 1000.0 / TargetFPS;

int     KeyBinds[(int)KeyBind::Max];

ISprite*    DEBUG_fontSprite = NULL;
void        DEBUG_DrawText(char* text, float x, float y) {
    for (char* i = text; *i; i++) {
        Graphics::DrawSprite(DEBUG_fontSprite, 0, (int)*i, x, y, false, false, 1.0f, 1.0f, 0.0f);
        x += 14; // DEBUG_fontSprite->Animations[0].Frames[(int)*i].ID;
    }
}

PUBLIC STATIC void Application::Init(int argc, char* args[]) {
#ifdef MSYS
    AllocConsole();
    freopen_s((FILE **)stdin, "CONIN$", "w", stdin);
    freopen_s((FILE **)stdout, "CONOUT$", "w", stdout);
    freopen_s((FILE **)stderr, "CONOUT$", "w", stderr);
#endif

    Log::Init();
    MemoryPools::Init();

    SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "1");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");

    #ifdef IOS
    // SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown"); // iOS only
    // SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "playback"); // Background Playback
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "1");
    iOS_InitMediaPlayer();
    #endif

    #ifdef ANDROID
    SDL_SetHint(SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH, "1");
    #endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        Log::Print(Log::LOG_INFO, "SDL_Init failed with error: %s", SDL_GetError());
    }

    SDL_SetEventFilter(Application::HandleAppEvents, NULL);

    Application::InitSettings("config.ini");

    Graphics::ChooseBackend();

    Application::Settings->GetBool("dev", "writeToFile", &Log::WriteToFile);

    bool allowRetina = false;
    Application::Settings->GetBool("display", "retina", &allowRetina);

    int defaultMonitor = Application::DefaultMonitor;

    Uint32 window_flags = 0;
    window_flags |= SDL_WINDOW_SHOWN;
    window_flags |= Graphics::GetWindowFlags();
    if (allowRetina)
        window_flags |= SDL_WINDOW_ALLOW_HIGHDPI;

    Application::Window = SDL_CreateWindow(NULL,
        SDL_WINDOWPOS_CENTERED_DISPLAY(defaultMonitor), SDL_WINDOWPOS_CENTERED_DISPLAY(defaultMonitor),
        Application::WindowWidth, Application::WindowHeight, window_flags);

    if (Application::Platform == Platforms::iOS) {
        // SDL_SetWindowFullscreen(Application::Window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_SetWindowFullscreen(Application::Window, SDL_WINDOW_FULLSCREEN);
    }
    else if (Application::Platform == Platforms::Switch) {
        SDL_SetWindowFullscreen(Application::Window, SDL_WINDOW_FULLSCREEN);
        // SDL_GetWindowSize(Application::Window, &Application::WIDTH, &Application::HEIGHT);
        AudioManager::MasterVolume = 0.25;

        #ifdef SWITCH
        SDL_DisplayMode mode;
        SDL_GetDisplayMode(0, 1 - appletGetOperationMode(), &mode);
        // SDL_SetWindowDisplayMode(Application::Window, &mode);
        Log::Print(Log::LOG_INFO, "Display Mode: %i x %i", mode.w, mode.h);
        #endif
    }

    // Initialize subsystems
    Math::Init();
    Graphics::Init();
    if (argc > 1 && !!StringUtils::StrCaseStr(args[1], ".hatch"))
        ResourceManager::Init(args[1]);
    else
        ResourceManager::Init(NULL);
    AudioManager::Init();
    InputManager::Init();
    Clock::Init();

    Application::LoadGameConfig();
    Application::ReadSettings();
    Application::DisposeGameConfig();

    const char *platform;
    switch (Application::Platform) {
        case Platforms::Windows:
            platform = "Windows"; break;
        case Platforms::MacOSX:
            platform = "MacOS"; break;
        case Platforms::Linux:
            platform = "Linux"; break;
        case Platforms::Switch:
            platform = "Nintendo Switch"; break;
        case Platforms::Playstation:
            platform = "Playstation"; break;
        case Platforms::Xbox:
            platform = "Xbox"; break;
        case Platforms::Android:
            platform = "Android"; break;
        case Platforms::iOS:
            platform = "iOS"; break;
        default:
            platform = "Unknown"; break;
    }
    Log::Print(Log::LOG_INFO, "Current Platform: %s", platform);

    Application::SetWindowTitle("Hatch Game Engine");

    Running = true;
}

PUBLIC STATIC bool Application::IsMobile() {
    return Application::Platform == Platforms::iOS || Application::Platform == Platforms::Android;
}

bool    AutomaticPerformanceSnapshots = false;
double  AutomaticPerformanceSnapshotFrameTimeThreshold = 20.0;
double  AutomaticPerformanceSnapshotLastTime = 0.0;
double  AutomaticPerformanceSnapshotMinInterval = 5000.0; // 5 seconds

int     MetricFrameCounterTime = 0;
double  MetricEventTime = -1;
double  MetricAfterSceneTime = -1;
double  MetricPollTime = -1;
double  MetricUpdateTime = -1;
double  MetricClearTime = -1;
double  MetricRenderTime = -1;
double  MetricFPSCounterTime = -1;
double  MetricPresentTime = -1;
double  MetricFrameTime = 0.0;
vector<ObjectList*> ListList;
PUBLIC STATIC void Application::GetPerformanceSnapshot() {
    if (Scene::ObjectLists) {
        // General Performance Snapshot
        double types[] = {
            MetricEventTime,
            MetricAfterSceneTime,
            MetricPollTime,
            MetricUpdateTime,
            MetricClearTime,
            MetricRenderTime,
            // MetricFPSCounterTime,
            MetricPresentTime,
            0.0,
            MetricFrameTime,
            FPS,
        };
        const char* typeNames[] = {
            "Event Polling:         %8.3f ms",
            "Garbage Collector:     %8.3f ms",
            "Input Polling:         %8.3f ms",
            "Entity Update:         %8.3f ms",
            "Clear Time:            %8.3f ms",
            "World Render Commands: %8.3f ms",
            // "FPS Counter Time:      %8.3f ms",
            "Frame Present Time:    %8.3f ms",
            "==================================",
            "Frame Total Time:      %8.3f ms",
            "FPS:                   %11.3f",
        };

        ListList.clear();
        Scene::ObjectLists->WithAll([](Uint32, ObjectList* list) -> void {
            if ((list->AverageUpdateTime > 0.0 && list->AverageUpdateItemCount > 0) ||
                (list->AverageRenderTime > 0.0 && list->AverageRenderItemCount > 0))
                ListList.push_back(list);
        });
        std::sort(ListList.begin(), ListList.end(), [](ObjectList* a, ObjectList* b) -> bool {
            return
                a->AverageUpdateTime * a->AverageUpdateItemCount + a->AverageRenderTime * a->AverageRenderItemCount >
                b->AverageUpdateTime * b->AverageUpdateItemCount + b->AverageRenderTime * b->AverageRenderItemCount;
        });

        Log::Print(Log::LOG_IMPORTANT, "General Performance Snapshot:");
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
            Log::Print(Log::LOG_INFO, typeNames[i], types[i]);
        }

        // View Rendering Performance Snapshot
        char layerText[2048];
        Log::Print(Log::LOG_IMPORTANT, "View Rendering Performance Snapshot:");
        for (int i = 0; i < MAX_SCENE_VIEWS; i++) {
            View* currentView = &Scene::Views[i];
            if (currentView->Active) {
                layerText[0] = 0;
                double tilesTotal = 0.0;
                for (size_t li = 0; li < Scene::Layers.size(); li++) {
                    SceneLayer* layer = &Scene::Layers[li];
                    char temp[128];
                    snprintf(temp, sizeof(temp), "     > %24s:   %8.3f ms\n",
                        layer->Name, Scene::PERF_ViewRender[i].LayerTileRenderTime[li]);
                    StringUtils::Concat(layerText, temp, sizeof(layerText));
                    tilesTotal += Scene::PERF_ViewRender[i].LayerTileRenderTime[li];
                }
                Log::Print(Log::LOG_INFO, "View %d:\n"
                    "           - Render Setup:        %8.3f ms %s\n"
                    "           - Projection Setup:    %8.3f ms\n"
                    "           - Object RenderEarly:  %8.3f ms\n"
                    "           - Object Render:       %8.3f ms\n"
                    "           - Object RenderLate:   %8.3f ms\n"
                    "           - Layer Tiles Total:   %8.3f ms\n%s"
                    "           - Finish:              %8.3f ms\n"
                    "           - Total:               %8.3f ms",
                    i,
                    Scene::PERF_ViewRender[i].RenderSetupTime, Scene::PERF_ViewRender[i].RecreatedDrawTarget ? "(recreated draw target)" : "",
                    Scene::PERF_ViewRender[i].ProjectionSetupTime,
                    Scene::PERF_ViewRender[i].ObjectRenderEarlyTime,
                    Scene::PERF_ViewRender[i].ObjectRenderTime,
                    Scene::PERF_ViewRender[i].ObjectRenderLateTime,
                    tilesTotal, layerText,
                    Scene::PERF_ViewRender[i].RenderFinishTime,
                    Scene::PERF_ViewRender[i].RenderTime);
            }
            // double RenderSetupTime;
            // bool   RecreatedDrawTarget;
            // double ProjectionSetupTime;
            // double ObjectRenderEarlyTime;
            // double ObjectRenderTime;
            // double ObjectRenderLateTime;
            // double LayerTileRenderTime[32]; // MAX_LAYERS
            // double RenderFinishTime;
            // double RenderTime;
        }

        // Object Performance Snapshot
        double totalUpdateEarly = 0.0;
        double totalUpdate = 0.0;
        double totalUpdateLate = 0.0;
        double totalRender = 0.0;
        Log::Print(Log::LOG_IMPORTANT, "Object Performance Snapshot:");
        for (size_t i = 0; i < ListList.size(); i++) {
            ObjectList* list = ListList[i];
            Log::Print(Log::LOG_INFO, "Object \"%s\":\n"
                "           - Avg Update Early %6.1f mcs (Total %6.1f mcs, Count %d)\n"
                "           - Avg Update       %6.1f mcs (Total %6.1f mcs, Count %d)\n"
                "           - Avg Update Late  %6.1f mcs (Total %6.1f mcs, Count %d)\n"
                "           - Avg Render       %6.1f mcs (Total %6.1f mcs, Count %d)", list->ObjectName,
                list->AverageUpdateEarlyTime * 1000.0, list->AverageUpdateEarlyTime * list->AverageUpdateEarlyItemCount * 1000.0, (int)list->AverageUpdateEarlyItemCount,
                list->AverageUpdateTime * 1000.0, list->AverageUpdateTime * list->AverageUpdateItemCount * 1000.0, (int)list->AverageUpdateItemCount,
                list->AverageUpdateLateTime * 1000.0, list->AverageUpdateLateTime * list->AverageUpdateLateItemCount * 1000.0, (int)list->AverageUpdateLateItemCount,
                list->AverageRenderTime * 1000.0, list->AverageRenderTime * list->AverageRenderItemCount * 1000.0, (int)list->AverageRenderItemCount);

            totalUpdateEarly += list->AverageUpdateEarlyTime * list->AverageUpdateEarlyItemCount * 1000.0;
            totalUpdate += list->AverageUpdateTime * list->AverageUpdateItemCount * 1000.0;
            totalUpdateLate += list->AverageUpdateLateTime * list->AverageUpdateLateItemCount * 1000.0;
            totalRender += list->AverageRenderTime * list->AverageRenderItemCount * 1000.0;
        }
        Log::Print(Log::LOG_WARN, "Total Update Early: %8.3f mcs / %1.3f ms", totalUpdateEarly, totalUpdateEarly / 1000.0);
        Log::Print(Log::LOG_WARN, "Total Update: %8.3f mcs / %1.3f ms", totalUpdate, totalUpdate / 1000.0);
        Log::Print(Log::LOG_WARN, "Total Update Late: %8.3f mcs / %1.3f ms", totalUpdateLate, totalUpdateLate / 1000.0);
        Log::Print(Log::LOG_WARN, "Total Render: %8.3f mcs / %1.3f ms", totalRender, totalRender / 1000.0);

        Log::Print(Log::LOG_IMPORTANT, "Garbage Size:");
        Log::Print(Log::LOG_INFO, "%u", (Uint32)GarbageCollector::GarbageSize);
    }
}

PUBLIC STATIC void Application::SetWindowTitle(const char* title) {
    memset(Application::WindowTitle, 0, sizeof(Application::WindowTitle));
    snprintf(Application::WindowTitle, sizeof(Application::WindowTitle), "%s", title);
    Application::UpdateWindowTitle();
}

PUBLIC STATIC void Application::UpdateWindowTitle() {
    char full[512];
    snprintf(full, sizeof(full), "%s", Application::WindowTitle);

    bool paren = false;

#define ADD_TEXT(text) \
    if (!paren) { \
        paren = true; \
        StringUtils::Concat(full, " (", sizeof(full)); \
    } \
    else StringUtils::Concat(full, ", ", sizeof(full)); \
    StringUtils::Concat(full, text, sizeof(full))

    if (DevMenu) {
        if (ResourceManager::UsingDataFolder) {
            ADD_TEXT("using Resources folder");
        }
        if (ResourceManager::UsingModPack) {
            ADD_TEXT("using Modpack");
        }
    }

    if (UpdatesPerFrame != 1) {
        ADD_TEXT("Frame Limit OFF");
    }

    switch (Scene::ShowTileCollisionFlag) {
        case 1:
            ADD_TEXT("Viewing Path A");
            break;
        case 2:
            ADD_TEXT("Viewing Path B");
            break;
    }

    if (Stepper) {
        ADD_TEXT("Frame Stepper ON");
    }
#undef ADD_TEXT

    if (paren)
        strcat(full, ")");

    SDL_SetWindowTitle(Application::Window, full);
}

PRIVATE STATIC void Application::Restart() {
    // Reset FPS timer
    BenchmarkFrameCount = 0;

    InputManager::ControllerStopRumble();

    Scene::Dispose();
    Graphics::SpriteSheetTextureMap->WithAll([](Uint32, Texture* tex) -> void {
        Graphics::DisposeTexture(tex);
    });
    Graphics::SpriteSheetTextureMap->Clear();

    BytecodeObjectManager::LoadAllClasses = false;
    Graphics::UseSoftwareRenderer = false;
    Graphics::UsePalettes = false;

    Application::LoadGameConfig();
    Application::ReloadSettings();
    Application::DisposeGameConfig();
}

#define CLAMP_VOLUME(vol) \
    if (vol < 0) \
        vol = 0; \
    else if (vol > 100) \
        vol = 100

PUBLIC STATIC void Application::SetMasterVolume(int volume) {
    CLAMP_VOLUME(volume);

    Application::MasterVolume = volume;
    AudioManager::MasterVolume = Application::MasterVolume / 100.0f;
}
PUBLIC STATIC void Application::SetMusicVolume(int volume) {
    CLAMP_VOLUME(volume);

    Application::MusicVolume = volume;
    AudioManager::MusicVolume = Application::MusicVolume / 100.0f;
}
PUBLIC STATIC void Application::SetSoundVolume(int volume) {
    CLAMP_VOLUME(volume);

    Application::SoundVolume = volume;
    AudioManager::SoundVolume = Application::SoundVolume / 100.0f;
}

PRIVATE STATIC void Application::LoadAudioSettings() {
    INI* settings = Application::Settings;

    int masterVolume = Application::MasterVolume;
    int musicVolume = Application::MusicVolume;
    int soundVolume = Application::SoundVolume;

#define GET_OR_SET_VOLUME(var) \
    if (!settings->PropertyExists("audio", #var)) \
        settings->SetInteger("audio", #var, var); \
    else \
        settings->GetInteger("audio", #var, &var)

    GET_OR_SET_VOLUME(masterVolume);
    GET_OR_SET_VOLUME(musicVolume);
    GET_OR_SET_VOLUME(soundVolume);

#undef GET_OR_SET_VOLUME

    Application::SetMasterVolume(masterVolume);
    Application::SetMusicVolume(musicVolume);
    Application::SetSoundVolume(soundVolume);
}

#undef CLAMP_VOLUME

SDL_Keycode KeyBindsSDL[(int)KeyBind::Max];

PRIVATE STATIC void Application::LoadKeyBinds() {
    XMLNode* node = nullptr;
    if (Application::GameConfig)
        node = XMLParser::SearchNode(Application::GameConfig->children[0], "keys");

    #define GET_KEY(setting, bind, def) { \
        char read[256] = {0}; \
        if (node) { \
            XMLNode* child = XMLParser::SearchNode(node, setting); \
            if (child) { \
                XMLParser::CopyTokenToString(child->children[0]->name, read, sizeof(read)); \
            } \
        } \
        Application::Settings->GetString("keys", setting, read, sizeof(read)); \
        int key = def; \
        if (read[0]) { \
            int parsed = InputManager::ParseKeyName(read); \
            if (parsed >= 0) \
                key = parsed; \
            else \
                key = Key_UNKNOWN; \
        } \
        Application::SetKeyBind((int)KeyBind::bind, key); \
    }

    GET_KEY("fullscreen",            Fullscreen,       Key_F4);
    GET_KEY("devRestartApp",         DevRestartApp,    Key_F1);
    GET_KEY("devRestartScene",       DevRestartScene,  Key_F6);
    GET_KEY("devRecompile",          DevRecompile,     Key_F5);
    GET_KEY("devPerfSnapshot",       DevPerfSnapshot,  Key_F3);
    GET_KEY("devLogLayerInfo",       DevLayerInfo,     Key_F2);
    GET_KEY("devFastForward",        DevFastForward,   Key_BACKSPACE);
    GET_KEY("devToggleFrameStepper", DevFrameStepper,  Key_F9);
    GET_KEY("devStepFrame",          DevStepFrame,     Key_F10);
    GET_KEY("devShowTileCol",        DevTileCol,       Key_F7);
    GET_KEY("devShowObjectRegions",  DevObjectRegions, Key_F8);
    GET_KEY("devQuit",               DevQuit,          Key_ESCAPE);

#undef GET_KEY
}

PRIVATE STATIC void Application::LoadDevSettings() {
    Application::Settings->GetBool("dev", "devMenu", &DevMenu);
    Application::Settings->GetBool("dev", "viewPerformance", &ShowFPS);
    Application::Settings->GetBool("dev", "donothing", &DoNothing);
    Application::Settings->GetInteger("dev", "fastforward", &UpdatesPerFastForward);
}

PUBLIC STATIC bool Application::IsWindowResizeable() {
    return !Application::IsMobile();
}

PUBLIC STATIC void Application::SetWindowSize(int window_w, int window_h) {
    if (!Application::IsWindowResizeable())
        return;

    SDL_SetWindowSize(Application::Window, window_w, window_h);

    int defaultMonitor = Application::DefaultMonitor;
    SDL_SetWindowPosition(Application::Window, SDL_WINDOWPOS_CENTERED_DISPLAY(defaultMonitor), SDL_WINDOWPOS_CENTERED_DISPLAY(defaultMonitor));

    // Incase the window just doesn't resize (Android)
    SDL_GetWindowSize(Application::Window, &window_w, &window_h);

    Graphics::Resize(window_w, window_h);
}

PUBLIC STATIC bool Application::GetWindowFullscreen() {
    return !!(SDL_GetWindowFlags(Application::Window) & SDL_WINDOW_FULLSCREEN_DESKTOP);
}

PUBLIC STATIC void Application::SetWindowFullscreen(bool isFullscreen) {
    SDL_SetWindowFullscreen(Application::Window, isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

    int window_w, window_h;
    SDL_GetWindowSize(Application::Window, &window_w, &window_h);

    Graphics::Resize(window_w, window_h);
}

PUBLIC STATIC void Application::SetWindowBorderless(bool isBorderless) {
    SDL_SetWindowBordered(Application::Window, (SDL_bool)(!isBorderless));
}

PUBLIC STATIC int  Application::GetKeyBind(int bind) {
    return KeyBinds[bind];
}
PUBLIC STATIC void Application::SetKeyBind(int bind, int key) {
    KeyBinds[bind] = key;
    if (key == Key_UNKNOWN)
        KeyBindsSDL[bind] = SDLK_UNKNOWN;
    else
        KeyBindsSDL[bind] = SDL_GetKeyFromScancode(InputManager::KeyToSDLScancode[key]);
}

PRIVATE STATIC void Application::PollEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: {
                Running = false;
                break;
            }
            case SDL_KEYDOWN: {
                SDL_Keycode key = e.key.keysym.sym;

                // Fullscreen
                if (key == KeyBindsSDL[(int)KeyBind::Fullscreen]) {
                    Application::SetWindowFullscreen(!Application::GetWindowFullscreen());
                    break;
                }

                if (DevMenu) {
                    // Quit game (dev)
                    if (key == KeyBindsSDL[(int)KeyBind::DevQuit]) {
                        Running = false;
                        break;
                    }
                    // Restart application (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevRestartApp]) {
                        Application::Restart();

                        Scene::Init();
                        if (*StartingScene)
                            Scene::LoadScene(StartingScene);
                        Scene::Restart();
                        Application::UpdateWindowTitle();
                        break;
                    }
                    // Show layer info (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevLayerInfo]) {
                        for (size_t li = 0; li < Scene::Layers.size(); li++) {
                            SceneLayer layer = Scene::Layers[li];
                            Log::Print(Log::LOG_IMPORTANT, "%2d: %20s (Visible: %d, Width: %d, Height: %d, OffsetX: %d, OffsetY: %d, RelativeY: %d, ConstantY: %d, DrawGroup: %d, ScrollDirection: %d, Flags: %d)", li,
                                layer.Name,
                                layer.Visible,
                                layer.Width,
                                layer.Height,
                                layer.OffsetX,
                                layer.OffsetY,
                                layer.RelativeY,
                                layer.ConstantY,
                                layer.DrawGroup,
                                layer.DrawBehavior,
                                layer.Flags);
                        }
                        break;
                    }
                    // Print performance snapshot (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevPerfSnapshot]) {
                        TakeSnapshot = true;
                        break;
                    }
                    // Recompile and restart scene (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevRecompile]) {
                        Application::Restart();

                        char temp[256];
                        memcpy(temp, Scene::CurrentScene, 256);

                        Scene::Init();

                        memcpy(Scene::CurrentScene, temp, 256);
                        Scene::LoadScene(Scene::CurrentScene);

                        Scene::Restart();
                        Application::UpdateWindowTitle();
                        break;
                    }
                    // Restart scene (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevRestartScene]) {
                        // Reset FPS timer
                        BenchmarkFrameCount = 0;

                        InputManager::ControllerStopRumble();

                        Scene::Restart();
                        Application::UpdateWindowTitle();
                        break;
                    }
                    // Enable update speedup (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevFastForward]) {
                        if (UpdatesPerFrame == 1)
                            UpdatesPerFrame = UpdatesPerFastForward;
                        else
                            UpdatesPerFrame = 1;

                        Application::UpdateWindowTitle();
                        break;
                    }
                    // Cycle view tile collision (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevTileCol]) {
                        Scene::ShowTileCollisionFlag = (Scene::ShowTileCollisionFlag + 1) % 3;
                        Application::UpdateWindowTitle();
                        break;
                    }
                    // View object regions (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevObjectRegions]) {
                        Scene::ShowObjectRegions ^= 1;
                        Application::UpdateWindowTitle();
                        break;
                    }
                    // Toggle frame stepper (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevFrameStepper]) {
                        Stepper = !Stepper;
                        MetricFrameCounterTime = 0;
                        Application::UpdateWindowTitle();
                        break;
                    }
                    // Step frame (dev)
                    else if (key == KeyBindsSDL[(int)KeyBind::DevStepFrame]) {
                        Stepper = true;
                        Step = true;
                        MetricFrameCounterTime++;
                        Application::UpdateWindowTitle();
                        break;
                    }
                }
                break;
            }
            case SDL_WINDOWEVENT: {
                switch (e.window.event) {
                    case SDL_WINDOWEVENT_RESIZED:
                        Graphics::Resize(e.window.data1, e.window.data2);
                        break;
                }
                break;
            }
            case SDL_CONTROLLERDEVICEADDED: {
                int i = e.cdevice.which;
                Log::Print(Log::LOG_VERBOSE, "Added controller device %d", i);
                InputManager::AddController(i);
                break;
            }
            case SDL_CONTROLLERDEVICEREMOVED: {
                int i = e.cdevice.which;
                Log::Print(Log::LOG_VERBOSE, "Removed controller device %d", i);
                InputManager::RemoveController(i);
                break;
            }
        }
    }
}
PRIVATE STATIC void Application::RunFrame(void* p) {
    FrameTimeStart = Clock::GetTicks();

    // Event loop
    MetricEventTime = Clock::GetTicks();
    Application::PollEvents();
    MetricEventTime = Clock::GetTicks() - MetricEventTime;

    // BUG: Having Stepper on prevents the first
    //   frame of a new scene from Updating, but still rendering.
    if (*Scene::NextScene)
        Step = true;

    MetricAfterSceneTime = Clock::GetTicks();
    Scene::AfterScene();
    MetricAfterSceneTime = Clock::GetTicks() - MetricAfterSceneTime;

    if (DoNothing) goto DO_NOTHING;

    // Update
    for (int m = 0; m < UpdatesPerFrame; m++) {
        Scene::ResetPerf();
        MetricPollTime = 0.0;
        MetricUpdateTime = 0.0;
        if ((Stepper && Step) || !Stepper) {
            // Poll for inputs
            MetricPollTime = Clock::GetTicks();
            InputManager::Poll();
            MetricPollTime = Clock::GetTicks() - MetricPollTime;

            // Update scene
            MetricUpdateTime = Clock::GetTicks();
            Scene::Update();
            MetricUpdateTime = Clock::GetTicks() - MetricUpdateTime;
        }
        Step = false;
    }

    // Rendering
    MetricClearTime = Clock::GetTicks();
    Graphics::Clear();
    MetricClearTime = Clock::GetTicks() - MetricClearTime;

    MetricRenderTime = Clock::GetTicks();
    Scene::Render();
    MetricRenderTime = Clock::GetTicks() - MetricRenderTime;

    DO_NOTHING:

    // Show FPS counter
    MetricFPSCounterTime = Clock::GetTicks();
    if (ShowFPS) {
        if (!DEBUG_fontSprite) {
            bool original = Graphics::TextureInterpolate;
            Graphics::SetTextureInterpolation(true);

            DEBUG_fontSprite = new ISprite();

            int cols, rows;
            DEBUG_fontSprite->SpritesheetCount = 1;
            DEBUG_fontSprite->Spritesheets[0] = DEBUG_fontSprite->AddSpriteSheet("Debug/Font.png");
            cols = DEBUG_fontSprite->Spritesheets[0]->Width / 32;
            rows = DEBUG_fontSprite->Spritesheets[0]->Height / 32;

            DEBUG_fontSprite->ReserveAnimationCount(1);
            DEBUG_fontSprite->AddAnimation("Font?", 0, 0, cols * rows);
            for (int i = 0; i < cols * rows; i++) {
                DEBUG_fontSprite->AddFrame(0,
                    (i % cols) * 32,
                    (i / cols) * 32,
                    32, 32, 0, 0,
                    14);
            }

            Graphics::SetTextureInterpolation(original);
        }

        int ww, wh;
        char textBuffer[256];
        SDL_GetWindowSize(Application::Window, &ww, &wh);
        Graphics::SetViewport(0.0, 0.0, ww, wh);
        Graphics::UpdateOrthoFlipped(ww, wh);

        Graphics::SetBlendMode(BlendFactor_SRC_ALPHA, BlendFactor_INV_SRC_ALPHA, BlendFactor_SRC_ALPHA, BlendFactor_INV_SRC_ALPHA);

        float infoW = 400.0;
        float infoH = 290.0;
        float infoPadding = 20.0;
        Graphics::Save();
        Graphics::Translate(0.0, 0.0, 0.0);
            Graphics::SetBlendColor(0.0, 0.0, 0.0, 0.75);
            Graphics::FillRectangle(0.0f, 0.0f, infoW, infoH);

            double types[] = {
                MetricEventTime,
                MetricAfterSceneTime,
                MetricPollTime,
                MetricUpdateTime,
                MetricClearTime,
                MetricRenderTime,
                MetricPresentTime,
            };
            const char* typeNames[] = {
                "Event Polling: %3.3f ms",
                "Garbage Collector: %3.3f ms",
                "Input Polling: %3.3f ms",
                "Entity Update: %3.3f ms",
                "Clear Time: %3.3f ms",
                "World Render Commands: %3.3f ms",
                "Frame Present Time: %3.3f ms",
            };
            struct { float r; float g; float b; } colors[8] = {
                { 1.0, 0.0, 0.0 },
                { 0.0, 1.0, 0.0 },
                { 0.0, 0.0, 1.0 },
                { 1.0, 1.0, 0.0 },
                { 0.0, 1.0, 1.0 },
                { 1.0, 0.0, 1.0 },
                { 1.0, 1.0, 1.0 },
                { 0.0, 0.0, 0.0 },
            };

            int typeCount = sizeof(types) / sizeof(double);


            Graphics::Save();
            Graphics::Translate(infoPadding - 2.0, infoPadding, 0.0);
            Graphics::Scale(0.85, 0.85, 1.0);
                snprintf(textBuffer, 256, "Frame Information");
                DEBUG_DrawText(textBuffer, 0.0, 0.0);
            Graphics::Restore();

            Graphics::Save();
            Graphics::Translate(infoW - infoPadding - (8 * 16.0 * 0.85), infoPadding, 0.0);
            Graphics::Scale(0.85, 0.85, 1.0);
                snprintf(textBuffer, 256, "FPS: %03.1f", FPS);
                DEBUG_DrawText(textBuffer, 0.0, 0.0);
            Graphics::Restore();

            if (Application::Platform == Platforms::Android || true) {
                // Draw bar
                float total = 0.0001;
                for (int i = 0; i < typeCount; i++) {
                    if (types[i] < 0.0)
                        types[i] = 0.0;
                    total += types[i];
                }

                Graphics::Save();
                Graphics::Translate(infoPadding, 50.0, 0.0);
                    Graphics::SetBlendColor(0.0, 0.0, 0.0, 0.25);
                    Graphics::FillRectangle(0.0, 0.0f, infoW - infoPadding * 2, 30.0);
                Graphics::Restore();

                float rectx = 0.0;
                for (int i = 0; i < typeCount; i++) {
                    Graphics::Save();
                    Graphics::Translate(infoPadding, 50.0, 0.0);
                        if (i < 8)
                            Graphics::SetBlendColor(colors[i].r, colors[i].g, colors[i].b, 0.5);
                        else
                            Graphics::SetBlendColor(0.5, 0.5, 0.5, 0.5);
                        Graphics::FillRectangle(rectx, 0.0f, types[i] / total * (infoW - infoPadding * 2), 30.0);
                    Graphics::Restore();

                    rectx += types[i] / total * (infoW - infoPadding * 2);
                }

                // Draw list
                float listY = 90.0;
                float totalFrameCount = 0.0f;
                infoPadding += infoPadding;
                for (int i = 0; i < typeCount; i++) {
                    Graphics::Save();
                    Graphics::Translate(infoPadding, listY, 0.0);
                        Graphics::SetBlendColor(colors[i].r, colors[i].g, colors[i].b, 0.5);
                        Graphics::FillRectangle(-infoPadding / 2.0, 0.0, 12.0, 12.0);
                    Graphics::Scale(0.6, 0.6, 1.0);
                        snprintf(textBuffer, 256, typeNames[i], types[i]);
                        DEBUG_DrawText(textBuffer, 0.0, 0.0);
                        listY += 20.0;
                    Graphics::Restore();

                    totalFrameCount += types[i];
                }

                // Draw total
                Graphics::Save();
                Graphics::Translate(infoPadding, listY, 0.0);
                    Graphics::SetBlendColor(1.0, 1.0, 1.0, 0.5);
                    Graphics::FillRectangle(-infoPadding / 2.0, 0.0, 12.0, 12.0);
                Graphics::Scale(0.6, 0.6, 1.0);
                    snprintf(textBuffer, 256, "Total Frame Time: %.3f ms", totalFrameCount);
                    DEBUG_DrawText(textBuffer, 0.0, 0.0);
                    listY += 20.0;
                Graphics::Restore();

                // Draw Overdelay
                Graphics::Save();
                Graphics::Translate(infoPadding, listY, 0.0);
                    Graphics::SetBlendColor(1.0, 1.0, 1.0, 0.5);
                    Graphics::FillRectangle(-infoPadding / 2.0, 0.0, 12.0, 12.0);
                Graphics::Scale(0.6, 0.6, 1.0);
                    snprintf(textBuffer, 256, "Overdelay: %.3f ms", Overdelay);
                    DEBUG_DrawText(textBuffer, 0.0, 0.0);
                    listY += 20.0;
                Graphics::Restore();

                float count = (float)Memory::MemoryUsage;
                const char* moniker = "B";

                if (count >= 1000000000) {
                    count /= 1000000000;
                    moniker = "GB";
                }
                else if (count >= 1000000) {
                    count /= 1000000;
                    moniker = "MB";
                }
                else if (count >= 1000) {
                    count /= 1000;
                    moniker = "KB";
                }

                listY += 30.0 - 20.0;

                Graphics::Save();
                Graphics::Translate(infoPadding / 2.0, listY, 0.0);
                Graphics::Scale(0.6, 0.6, 1.0);
                    snprintf(textBuffer, 256, "RAM Usage: %.3f %s", count, moniker);
                    DEBUG_DrawText(textBuffer, 0.0, 0.0);
                Graphics::Restore();

                listY += 30.0;

                float* listYPtr = &listY;
                if (Scene::ObjectLists && Application::Platform != Platforms::Android) {
                    Scene::ObjectLists->WithAll([infoPadding, listYPtr](Uint32, ObjectList* list) -> void {
                        char textBufferXXX[1024];
                        if (list->AverageUpdateItemCount > 0.0) {
                            Graphics::Save();
                            Graphics::Translate(infoPadding / 2.0, *listYPtr, 0.0);
                            Graphics::Scale(0.6, 0.6, 1.0);
                                // snprintf(textBufferXXX, 1024, "Object \"%s\": Avg Update %.1f mcs (Total %.1f mcs, Count %d)", list->ObjectName, list->AverageUpdateTime * 1000.0, list->AverageUpdateTime * list->AverageUpdateItemCount * 1000.0, (int)list->AverageUpdateItemCount);
                                snprintf(textBufferXXX, 1024, "Object \"%s\": Avg Render %.1f mcs (Total %.1f mcs, Count %d)", list->ObjectName, list->AverageRenderTime * 1000.0, list->AverageRenderTime * list->AverageRenderItemCount * 1000.0, (int)list->AverageRenderItemCount);
                                DEBUG_DrawText(textBufferXXX, 0.0, 0.0);
                            Graphics::Restore();

                            *listYPtr += 20.0;
                        }
                    });
                }
            }
        Graphics::Restore();
    }
    MetricFPSCounterTime = Clock::GetTicks() - MetricFPSCounterTime;

    MetricPresentTime = Clock::GetTicks();
    Graphics::Present();
    MetricPresentTime = Clock::GetTicks() - MetricPresentTime;

    MetricFrameTime = Clock::GetTicks() - FrameTimeStart;
}
PRIVATE STATIC void Application::DelayFrame() {
    // HACK: MacOS V-Sync timing gets disabled if window is not visible
    if (!Graphics::VsyncEnabled || Application::Platform == Platforms::MacOSX) {
        double frameTime = Clock::GetTicks() - FrameTimeStart;
        double frameDurationRemainder = FrameTimeDesired - frameTime;
        if (frameDurationRemainder >= 0.0) {
            // NOTE: Delay duration will always be more than requested wait time.
            if (frameDurationRemainder > 1.0) {
                double delayStartTime = Clock::GetTicks();

                Clock::Delay(frameDurationRemainder - 1.0);

                double delayTime = Clock::GetTicks() - delayStartTime;
                Overdelay = delayTime - (frameDurationRemainder - 1.0);
            }

            // frameDurationRemainder = floor(frameDurationRemainder);
            // if (delayTime > frameDurationRemainder)
            //     printf("delayTime: %.3f   frameDurationRemainder: %.3f\n", delayTime, frameDurationRemainder);

            while ((Clock::GetTicks() - FrameTimeStart) < FrameTimeDesired);
        }
    }
    else {
        Clock::Delay(1);
    }
}
PUBLIC STATIC void Application::Run(int argc, char* args[]) {
    Application::Init(argc, args);
    if (!Running)
        return;

    Scene::Init();

    if (argc > 1) {
        char* pathStart = StringUtils::StrCaseStr(args[1], "/Resources/");
        if (pathStart == NULL)
            pathStart = StringUtils::StrCaseStr(args[1], "\\Resources\\");

        if (pathStart) {
            char* tmxPath = pathStart + strlen("/Resources/");
            for (char* i = tmxPath; *i; i++) {
                if (*i == '\\')
                    *i = '/';
            }
            Scene::LoadScene(tmxPath);
        }
        else {
            Log::Print(Log::LOG_WARN, "Map file \"%s\" not inside Resources folder!", args[1]);
        }
    }
    else if (*StartingScene) {
        Scene::LoadScene(StartingScene);
    }

    Scene::Restart();
    Application::UpdateWindowTitle();
    Application::SetWindowSize(Application::WindowWidth, Application::WindowHeight);

    Graphics::Clear();
    Graphics::Present();

    #ifdef IOS
        // Initialize the Game Center for scoring and matchmaking
        // InitGameCenter();

        // Set up the game to run in the window animation callback on iOS
        // so that Game Center and so forth works correctly.
        SDL_iPhoneSetAnimationCallback(Application::Window, 1, RunFrame, NULL);
    #else
        while (Running) {
            if (BenchmarkFrameCount == 0)
                BenchmarkTickStart = Clock::GetTicks();

            Application::RunFrame(NULL);
            Application::DelayFrame();

            BenchmarkFrameCount++;
            if (BenchmarkFrameCount == TargetFPS) {
                double measuredSecond = Clock::GetTicks() - BenchmarkTickStart;
                FPS = 1000.0 / floor(measuredSecond) * TargetFPS;
                BenchmarkFrameCount = 0;
            }

            if (AutomaticPerformanceSnapshots && MetricFrameTime > AutomaticPerformanceSnapshotFrameTimeThreshold) {
                if (Clock::GetTicks() - AutomaticPerformanceSnapshotLastTime > AutomaticPerformanceSnapshotMinInterval) {
                    AutomaticPerformanceSnapshotLastTime = Clock::GetTicks();
                    TakeSnapshot = true;
                }
            }

            if (TakeSnapshot) {
                TakeSnapshot = false;
                Application::GetPerformanceSnapshot();
            }
        }

        Scene::Dispose();

        if (DEBUG_fontSprite) {
            DEBUG_fontSprite->Dispose();
            delete DEBUG_fontSprite;
            DEBUG_fontSprite = NULL;
        }

        Application::Cleanup();

        Memory::PrintLeak();
    #endif
}

PUBLIC STATIC void Application::Cleanup() {
    ResourceManager::Dispose();
    AudioManager::Dispose();
    InputManager::Dispose();

    Graphics::Dispose();

    SDL_DestroyWindow(Application::Window);

    SDL_Quit();

    // Memory::PrintLeak();

#ifdef MSYS
    FreeConsole();
#endif
}

static void ParseGameConfigInt(XMLNode* parent, const char* option, int& val) {
    XMLNode* node = XMLParser::SearchNode(parent, option);
    if (!node)
        return;

    char read[32];
    XMLParser::CopyTokenToString(node->children[0]->name, read, sizeof(read));
    StringUtils::ToNumber(&val, read);
}
static void ParseGameConfigBool(XMLNode* node, const char* option, bool& val) {
    node = XMLParser::SearchNode(node, option);
    if (!node)
        return;

    char read[5];
    XMLParser::CopyTokenToString(node->children[0]->name, read, sizeof(read));
    val = !strcmp(read, "true");
}

PRIVATE STATIC void Application::LoadGameConfig() {
    StartingScene[0] = '\0';

    Application::GameConfig = XMLParser::ParseFromResource("GameConfig.xml");
    if (!Application::GameConfig)
        return;
    XMLNode* root = Application::GameConfig->children[0];
    XMLNode* node;

    // Read engine settings
    node = XMLParser::SearchNode(root, "engine");
    if (node) {
        ParseGameConfigBool(node, "loadAllClasses", BytecodeObjectManager::LoadAllClasses);
        ParseGameConfigBool(node, "useSoftwareRenderer", Graphics::UseSoftwareRenderer);
        ParseGameConfigBool(node, "enablePaletteUsage", Graphics::UsePalettes);
    }

    // Read display defaults
    node = XMLParser::SearchNode(root, "display");
    if (node) {
        ParseGameConfigInt(node, "width", Application::WindowWidth);
        ParseGameConfigInt(node, "height", Application::WindowHeight);
    }

    // Read audio defaults
#define GET_VOLUME(node, func) \
    if (node->attributes.Exists("volume")) { \
        int volume; \
        char xmlTokStr[32]; \
        XMLParser::CopyTokenToString(node->attributes.Get("volume"), xmlTokStr, sizeof(xmlTokStr)); \
        if (StringUtils::ToNumber(&volume, xmlTokStr)) \
            Application::func(volume); \
    }
    node = XMLParser::SearchNode(root, "audio");
    if (node) {
        // Get master audio volume
        GET_VOLUME(node, SetMasterVolume);

        XMLNode* parent = node;

        // Get music volume
        node = XMLParser::SearchNode(parent, "music");
        if (node) {
            GET_VOLUME(node, SetMusicVolume);
        }

        // Get sound volume
        node = XMLParser::SearchNode(parent, "sound");
        if (node) {
            GET_VOLUME(node, SetSoundVolume);
        }
    }
#undef GET_VOLUME

    // Read starting scene
    node = XMLParser::SearchNode(root, "startscene");
    if (node)
        XMLParser::CopyTokenToString(node->children[0]->name, StartingScene, sizeof(StartingScene));
}

PRIVATE STATIC void Application::DisposeGameConfig() {
    if (Application::GameConfig)
        XMLParser::Free(Application::GameConfig);
    Application::GameConfig = nullptr;
}

PUBLIC STATIC bool Application::LoadSettings(const char* filename) {
    INI* ini = INI::Load(filename);
    if (ini == nullptr)
        return false;

    StringUtils::Copy(Application::SettingsFile, filename, sizeof(Application::SettingsFile));

    if (Application::Settings)
        Application::Settings->Dispose();
    Application::Settings = ini;

    return true;
}

PUBLIC STATIC void Application::ReadSettings() {
    Application::LoadAudioSettings();
    Application::LoadDevSettings();
    Application::LoadKeyBinds();
}

PUBLIC STATIC void Application::ReloadSettings() {
    if (Application::Settings && Application::Settings->Reload())
        Application::ReadSettings();
}

PUBLIC STATIC void Application::ReloadSettings(const char* filename) {
    if (Application::LoadSettings(filename))
        Application::ReadSettings();
}

PUBLIC STATIC void Application::InitSettings(const char* filename) {
    Application::LoadSettings(filename);

    // NOTE: If no settings could be loaded, create settings with default values.
    if (!Application::Settings) {
        Application::Settings = INI::New(Application::SettingsFile);

        Application::Settings->SetBool("display", "fullscreen", false);
        Application::Settings->SetBool("display", "vsync", false);
        Application::Settings->SetBool("display", "software", true);
    }

    int logLevel = 0;
#ifdef DEBUG
    logLevel = -1;
#endif
#ifdef ANDROID
    logLevel = -1;
 #endif
    Application::Settings->GetInteger("dev", "logLevel", &logLevel);
    Application::Settings->GetBool("dev", "trackMemory", &Memory::IsTracking);
    Log::SetLogLevel(logLevel);

    Application::Settings->GetBool("dev", "autoPerfSnapshots", &AutomaticPerformanceSnapshots);
    int apsFrameTimeThreshold = 20, apsMinInterval = 5;
    Application::Settings->GetInteger("dev", "apsMinFrameTime", &apsFrameTimeThreshold);
    Application::Settings->GetInteger("dev", "apsMinInterval", &apsMinInterval);
    AutomaticPerformanceSnapshotFrameTimeThreshold = apsFrameTimeThreshold;
    AutomaticPerformanceSnapshotMinInterval = apsMinInterval;

    Application::Settings->GetBool("display", "vsync", &Graphics::VsyncEnabled);
    Application::Settings->GetInteger("display", "multisample", &Graphics::MultisamplingEnabled);
    Application::Settings->GetInteger("display", "defaultMonitor", &Application::DefaultMonitor);
}
PUBLIC STATIC void Application::SaveSettings() {
    if (Application::Settings)
        Application::Settings->Save();
}
PUBLIC STATIC void Application::SaveSettings(const char* filename) {
    if (Application::Settings)
        Application::Settings->Save(filename);
}
PUBLIC STATIC void Application::SetSettingsFilename(const char* filename) {
    StringUtils::Copy(Application::SettingsFile, filename, sizeof(Application::SettingsFile));
    if (Application::Settings)
        Application::Settings->SetFilename(filename);
}

PRIVATE STATIC int Application::HandleAppEvents(void* data, SDL_Event* event) {
    switch (event->type) {
        case SDL_APP_TERMINATING:
            Log::Print(Log::LOG_VERBOSE, "SDL_APP_TERMINATING");
            Scene::OnEvent(event->type);
            return 0;
        case SDL_APP_LOWMEMORY:
            Log::Print(Log::LOG_VERBOSE, "SDL_APP_LOWMEMORY");
            Scene::OnEvent(event->type);
            return 0;
        case SDL_APP_WILLENTERBACKGROUND:
            Log::Print(Log::LOG_VERBOSE, "SDL_APP_WILLENTERBACKGROUND");
            Scene::OnEvent(event->type);
            return 0;
        case SDL_APP_DIDENTERBACKGROUND:
            Log::Print(Log::LOG_VERBOSE, "SDL_APP_DIDENTERBACKGROUND");
            Scene::OnEvent(event->type);
            return 0;
        case SDL_APP_WILLENTERFOREGROUND:
            Log::Print(Log::LOG_VERBOSE, "SDL_APP_WILLENTERFOREGROUND");
            Scene::OnEvent(event->type);
            return 0;
        case SDL_APP_DIDENTERFOREGROUND:
            Log::Print(Log::LOG_VERBOSE, "SDL_APP_DIDENTERFOREGROUND");
            Scene::OnEvent(event->type);
            return 0;
        default:
            return 1;
    }
}
