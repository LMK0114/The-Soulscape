#include <GL/glut.h>
#include <math.h>
#include <vector>
#include <algorithm> // Needed for std::rotate
#include <cstdlib>
#include <ctime>
#include <cstdio>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <map>

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")

// Simple Sound Manager Class
class SoundSystem {
private:
    struct SoundBuffer
    {
        LPDIRECTSOUNDBUFFER buffer;
        bool isPlaying;
        DWORD lastPlayTime;
    };

    static SoundSystem* instance;
    LPDIRECTSOUND8 directSound;
    LPDIRECTSOUNDBUFFER primaryBuffer;
    std::map<std::string, SoundBuffer> soundBuffers;
    bool soundEnabled;

    SoundSystem() : directSound(nullptr), primaryBuffer(nullptr), soundEnabled(true) {
        initializeDirectSound();
        preloadSounds();
    }

    void initializeDirectSound()
    {
        printf("Initializing DirectSound...\n");
        // Initialize DirectSound
        HRESULT hr = DirectSoundCreate8(NULL, &directSound, NULL);
        if (hr != DS_OK) {
            printf("ERROR: DirectSoundCreate8 failed: 0x%08X\n", hr);
            directSound = nullptr;
            return;
        }
        printf("DirectSound created successfully\n");

        // Set cooperative level
        HWND hwnd = GetForegroundWindow();
        if (!hwnd) hwnd = GetDesktopWindow(); // Fallback

        hr = directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
        if (hr != DS_OK) {
            printf("ERROR: SetCooperativeLevel failed: 0x%08X\n", hr);
            directSound->Release();
            directSound = nullptr;
            return;
        }
        printf("Cooperative level set successfully\n");

        // Create primary buffer
        DSBUFFERDESC bufferDesc = { sizeof(DSBUFFERDESC) };
        bufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRLVOLUME;
        bufferDesc.dwBufferBytes = 0;
        bufferDesc.lpwfxFormat = NULL;

        if (directSound->CreateSoundBuffer(&bufferDesc, &primaryBuffer, NULL) != DS_OK) {
            return;
        }

        // Set primary buffer format
        WAVEFORMATEX waveFormat;
        waveFormat.wFormatTag = WAVE_FORMAT_PCM;
        waveFormat.nSamplesPerSec = 44100;
        waveFormat.wBitsPerSample = 16;
        waveFormat.nChannels = 2;
        waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
        waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
        waveFormat.cbSize = 0;

        primaryBuffer->SetFormat(&waveFormat);
    }

    DWORD min(DWORD a, DWORD b)
    {
        return (a < b) ? a : b;
    }

    bool loadSound(const char* filename, const std::string& soundName) {
        if (!directSound)
        {
            printf("ERROR: DirectSound not initialized!\n");
            return false;
        }

        printf("Loading sound: %s as '%s'\n", filename, soundName.c_str());

        // Use Windows API to open file (more reliable)
        HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

        if (hFile == INVALID_HANDLE_VALUE) {
            printf("ERROR: Could not open file: %s (Error: %d)\n", filename, GetLastError());
            return false;
        }

        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize == INVALID_FILE_SIZE) {
            CloseHandle(hFile);
            return false;
        }

        // Read entire file
        BYTE* fileData = new BYTE[fileSize];
        DWORD bytesRead;
        if (!ReadFile(hFile, fileData, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
            delete[] fileData;
            CloseHandle(hFile);
            return false;
        }
        CloseHandle(hFile);

        // Check if it's a valid WAV file
        if (fileSize < 44 || // Minimum WAV header size
            fileData[0] != 'R' || fileData[1] != 'I' || fileData[2] != 'F' || fileData[3] != 'F' ||
            fileData[8] != 'W' || fileData[9] != 'A' || fileData[10] != 'V' || fileData[11] != 'E') {
            printf("ERROR: Not a valid WAV file: %s\n", filename);
            delete[] fileData;
            return false;
        }

        // Parse WAV header
        WAVEFORMATEX* waveFormat = NULL;
        BYTE* soundData = NULL;
        DWORD soundSize = 0;

        DWORD offset = 12; // Start after "RIFF" and size
        while (offset < fileSize - 8) {
            char chunkID[5];
            memcpy(chunkID, &fileData[offset], 4);
            chunkID[4] = '\0';
            DWORD chunkSize = *(DWORD*)&fileData[offset + 4];

            if (strcmp(chunkID, "fmt ") == 0) {
                // Found format chunk
                waveFormat = (WAVEFORMATEX*)new BYTE[sizeof(WAVEFORMATEX)];
                memcpy(waveFormat, &fileData[offset + 8], min(chunkSize, sizeof(WAVEFORMATEX)));
            }
            else if (strcmp(chunkID, "data") == 0) {
                // Found data chunk
                soundData = new BYTE[chunkSize];
                soundSize = chunkSize;
                memcpy(soundData, &fileData[offset + 8], chunkSize);
            }

            offset += 8 + chunkSize;
        }

        if (!waveFormat || !soundData) {
            printf("ERROR: Invalid WAV format for: %s\n", filename);
            if (waveFormat) delete waveFormat;
            if (soundData) delete[] soundData;
            delete[] fileData;
            return false;
        }

        printf("WAV Format: %dHz, %d-bit, %d channels\n",
            waveFormat->nSamplesPerSec,
            waveFormat->wBitsPerSample,
            waveFormat->nChannels);

        // Create DirectSound buffer
        DSBUFFERDESC bufferDesc = { sizeof(DSBUFFERDESC) };
        bufferDesc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
        bufferDesc.dwBufferBytes = soundSize;
        bufferDesc.lpwfxFormat = waveFormat;

        LPDIRECTSOUNDBUFFER buffer;
        HRESULT hr = directSound->CreateSoundBuffer(&bufferDesc, &buffer, NULL);

        if (hr != DS_OK) {
            printf("ERROR: CreateSoundBuffer failed: 0x%08X\n", hr);
            delete waveFormat;
            delete[] soundData;
            delete[] fileData;
            return false;
        }

        // Lock buffer and copy data
        void* bufferPtr1;
        void* bufferPtr2;
        DWORD bufferSize1;
        DWORD bufferSize2;

        hr = buffer->Lock(0, soundSize, &bufferPtr1, &bufferSize1, &bufferPtr2, &bufferSize2, 0);
        if (hr == DS_OK) {
            memcpy(bufferPtr1, soundData, bufferSize1);
            if (bufferPtr2 && bufferSize2 > 0) {
                memcpy(bufferPtr2, soundData + bufferSize1, bufferSize2);
            }
            buffer->Unlock(bufferPtr1, bufferSize1, bufferPtr2, bufferSize2);
        }
        else {
            printf("ERROR: Buffer lock failed: 0x%08X\n", hr);
            buffer->Release();
            delete waveFormat;
            delete[] soundData;
            delete[] fileData;
            return false;
        }

        // Store the buffer
        SoundBuffer soundBuffer;
        soundBuffer.buffer = buffer;
        soundBuffer.isPlaying = false;
        soundBuffer.lastPlayTime = 0;

        soundBuffers[soundName] = soundBuffer;

        // Cleanup
        delete waveFormat;
        delete[] soundData;
        delete[] fileData;

        printf("Successfully loaded sound: %s (size: %d bytes)\n", soundName.c_str(), soundSize);
        return true;
    }

    void preloadSounds()
    {
        // Test with absolute paths first
        std::string basePath = "sounds/effects/";

        // Check if directory exists
        DWORD attrs = GetFileAttributesA(basePath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
        {
            printf("ERROR: Sound directory not found! Current directory: ");

            // Get current directory
            char currentDir[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, currentDir);
            printf("%s\n", currentDir);

            // Try different paths
            basePath = "./sounds/effects/";
            attrs = GetFileAttributesA(basePath.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES)
            {
                printf("ERROR: ./sounds/effects/ also not found!\n");
            }
        }

        // Load all your sound files here
        loadSound("sounds/effects/jump.wav", "jump"); //DONE
        loadSound("sounds/effects/double_jump.wav", "double_jump"); //DONE
        loadSound("sounds/effects/land.wav", "land");
        loadSound("sounds/effects/hurt.wav", "hurt");//DONE
        loadSound("sounds/effects/platform_break.wav", "platform_break"); // HALF DONE
        loadSound("sounds/effects/platform_step.wav", "platform_step");

        // Power-up sounds
        loadSound("sounds/effects/powerup_speed.wav", "powerup_speed"); //DONE
        loadSound("sounds/effects/powerup_shield.wav", "powerup_shield");//DONE
        loadSound("sounds/effects/powerup_double.wav", "powerup_double");//DONE

        // Boss sounds
        loadSound("sounds/effects/laser_warning.wav", "laser_warning");//DONE
        loadSound("sounds/effects/laser_fire.wav", "laser_fire");//DONE
        loadSound("sounds/effects/boss_roar.wav", "boss_roar");

        // UI sounds
        loadSound("sounds/ui/game_start.wav", "game_start");//DONE
        loadSound("sounds/ui/game_over.wav", "game_over");//DONE
        loadSound("sounds/ui/pause.wav", "pause");//DONE
        loadSound("sounds/ui/unpause.wav", "unpause");//DONE
        loadSound("sounds/ui/select.wav", "select");//DONE
        loadSound("sounds/ui/arrowkeyselect.wav", "arrowkeyselect");//DONE
        loadSound("sounds/ui/new_highscore.wav", "new_highscore");

        // Music (these will need special handling for looping)
        loadSound("sounds/music/menu_bg.wav", "menu_music");//DONE
        loadSound("sounds/music/game_bg.wav", "game_music");//DONE
        loadSound("sounds/music/boss_bg.wav", "boss_music");//DONE
    }

public:
    static SoundSystem* getInstance() {
        if (!instance) {
            instance = new SoundSystem();
        }
        return instance;
    }

    void playSound(const std::string& soundName, bool loop = false)
    {
        if (!soundEnabled || !directSound)
        {
            printf("Sound disabled or DirectSound not initialized\n");
            return;
        }

        auto it = soundBuffers.find(soundName);
        if (it == soundBuffers.end())
        {
            printf("ERROR: Sound '%s' not found in buffers!\n", soundName.c_str());

            // List available sounds
            printf("Available sounds: ");
            for (const auto& pair : soundBuffers) {
                printf("%s ", pair.first.c_str());
            }
            printf("\n");
            return;
        }

        SoundBuffer& soundBuffer = it->second;

        // Check if sound is already playing (optional cooldown)
        DWORD status;
        HRESULT hr = soundBuffer.buffer->GetStatus(&status);

        // Stop the sound if it's already playing
        soundBuffer.buffer->Stop();
        soundBuffer.buffer->SetCurrentPosition(0);

        // Set playback flags
        DWORD flags = 0;
        if (loop) {
            flags = DSBPLAY_LOOPING;
        }

        hr = soundBuffer.buffer->Play(0, 0, flags);
        if (hr != DS_OK) {
            printf("ERROR: Failed to play sound '%s': 0x%08X\n", soundName.c_str(), hr);
        }
        else {
            printf("Playing sound: %s (loop: %s)\n", soundName.c_str(), loop ? "yes" : "no");
        }
    }

    void stopSound(const std::string& soundName) {
        auto it = soundBuffers.find(soundName);
        if (it != soundBuffers.end()) {
            it->second.buffer->Stop();
            it->second.isPlaying = false;
        }
    }

    void stopAllSounds() {
        for (auto& pair : soundBuffers) {
            pair.second.buffer->Stop();
            pair.second.isPlaying = false;
        }
    }

    // Update your sound methods to use the new system
    void playJumpSound() { playSound("jump"); }
    void playDoubleJumpSound() { playSound("double_jump"); }
    void playLandSound() { playSound("land"); }
    void playHurtSound() { playSound("hurt"); }
    void playPlatformBreakSound() { playSound("platform_break"); }
    void playPlatformStepSound() { playSound("platform_step"); }
    void playPowerUpSpeed() { playSound("powerup_speed"); }
    void playPowerUpShield() { playSound("powerup_shield"); }
    void playPowerUpDoubleJump() { playSound("powerup_double"); }
    void playLaserWarningSound() { playSound("laser_warning"); }
    void playLaserFireSound() { playSound("laser_fire"); }
    void playBossRoar() { playSound("boss_roar"); }
    void playGameStartSound() { playSound("game_start"); }
    void playGameOverSound() { playSound("game_over"); }
    void playPauseSound() { playSound("pause"); }
    void playUnPauseSound() { playSound("unpause"); }
    void playSelectSound() { playSound("select"); }
    void playArrowSelectSound() { playSound("arrowkeyselect"); }
    void playNewHighscoreSound() { playSound("new_highscore"); }

    void playMenuMusic() {
        stopSound("game_music");
        stopSound("boss_music");
        playSound("menu_music", true);
    }

    void playGameMusic() {
        stopSound("menu_music");
        stopSound("boss_music");
        playSound("game_music", true);
    }

    void playBossMusic() {
        stopSound("menu_music");
        stopSound("game_music");
        playSound("boss_music", true);
    }

    void cleanup() {
        stopAllSounds();
        for (auto& pair : soundBuffers) {
            if (pair.second.buffer) {
                pair.second.buffer->Release();
            }
        }
        soundBuffers.clear();

        if (primaryBuffer) {
            primaryBuffer->Release();
            primaryBuffer = nullptr;
        }

        if (directSound) {
            directSound->Release();
            directSound = nullptr;
        }
    }
};

SoundSystem* SoundSystem::instance = nullptr;

// ==========================================
// END OF SOUND SYSTEM
// ==========================================

void initializeGamePlatforms();
void drawExitConfirmation();

// ==========================================
// 1. STRUCTS & GLOBALS
// ==========================================

enum GameState
{
    MENU,
    TRANSITION,  // ADD THIS: Transition from menu to game
    PLAYING,
    GAMEOVER,
    PAUSED
};

// NEW: Power-up types
enum PowerUpType
{
    POWERUP_NONE,
    POWERUP_SPEED_BOOST,     // Faster movement and jumps
    POWERUP_DOUBLE_JUMP,     // Can jump mid-air once
    POWERUP_SHIELD,          // Temporary invincibility
    POWERUP_SLOW_TIME,       // Slows down boss and platforms temporarily
    POWERUP_MAGNET           // Attracts nearby power-ups
};

struct Player
{
    float x, y;
    float velocityX, velocityY;
    bool isGrounded;
    bool isJumping;
    float coyoteTimer;

    // NEW: Power-up related stats
    bool hasDoubleJump;
    bool doubleJumpUsed;
    float speedBoostTimer;
    float shieldTimer;
    float slowTimeTimer;
    bool hasMagnet;
    float magnetTimer;

    // NEW: Stun mechanic
    float stunTimer;         // Time player is stunned
    bool isStunned;          // Is player currently stunned

    // Visual effects for power-ups
    float shieldAlpha;
    float speedTrailTimer;

    // Invisibility mechanic
    float invisibilityTimer;      // Time remaining for invisibility
    bool isInvisible;            // Is player currently invisible
    float invisibilityAlpha;     // Visual alpha for player (0.0 = fully invisible, 1.0 = visible)
};

struct Platform
{
    float x, y;
    float width;
    bool isBreakable;
    float breakTimer;
    bool active;
    bool steppedOn;
};

// NEW: Power-up struct
struct PowerUp
{
    float x, y;
    float velocityY;
    bool active;
    PowerUpType type;
    float rotation;
    float bobOffset;
    float spawnTime;
    float glowIntensity;
};

// NEW: Fire Laser struct for boss attack
struct FireLaser
{
    float x;                 // X position of laser
    float y;                 // Current Y position
    float speed;             // Speed of laser movement
    bool active;             // Is laser active
    float width;             // Width of laser
    float timer;             // Timer for laser existence
    float maxTimer;          // Maximum time laser exists
    float chargeTimer;       // Timer for charging effect
    float glowIntensity;     // Glow effect intensity
    bool isWarning;          // ADD THIS: Is this a warning or actual laser
    float warningProgress;   // ADD THIS: Progress of warning (0-1)
    float symbolScale;      // ADD THIS for symbol animation
};

// NEW: Particle Struct for Optimization #3
struct Particle
{
    float x, y;
    float alpha;
    float size;
    float velocityX, velocityY;
    float color[3];
};

// --- GLOBAL VARIABLES ---
GameState currentState = MENU;
Player soul;
std::vector<Platform> platforms;
std::vector<Particle> particles; // Particle Pool
std::vector<PowerUp> powerUps; // NEW: Power-ups collection
std::vector<FireLaser> fireLasers; // NEW: Boss fire lasers
std::vector<float> laserWarningPositions; // Store warning positions

bool keyStates[256];
float g_Time = 0.0f;

// Pause menu variables
bool isPausedKeyPressed = false;  // To prevent holding ESC from toggling repeatedly
float pauseMenuAlpha = 0.0f;      // For fade-in effect
const float PAUSE_FADE_SPEED = 5.0f; // Speed of fade animation

bool showExitDialog = false;

// Pause menu selection
int pauseMenuSelection = 0;  // 0: Resume, 1: Restart, 2: Main Menu
const int PAUSE_OPTIONS_COUNT = 3;

float bossY = -300.0f;
float bossX = 400.0f;
float totalScore = 0.0f;
float bestDistance = 0.0f;
float landSquash = 0.0f;
float lastSpawnX = 400.0f;
const float CHANCE_2_PLATFORMS = 0.4f; // 40% chance for 2 platforms
const float CHANCE_3_PLATFORMS = 0.4f; // 40% chance for 3 platforms

// NEW: Power-up global effects
float globalSlowFactor = 1.0f; // 1.0 = normal speed, 0.5 = half speed
float slowTimeEffectTimer = 0.0f;
float magnetRange = 0.0f; // For magnet power-up effect radius

// NEW: Boss attack variables
float bossAttackCooldown = 0.0f;     // Cooldown between attacks
const float BOSS_ATTACK_COOLDOWN_MAX = 10.0f; // Time between attacks
float bossAttackWarningTimer = 0.0f; // Timer for attack warning
float bossAttackWarningX = 0.0f;     // X position for attack warning
bool isWarningPhase = false;              // Is boss in warning phase?
float warningPhaseTimer = 0.0f;           // Timer for warning phase

// NEW: Display List ID for Optimization #2
GLuint platformDisplayList;
GLuint powerUpDisplayList; // For power-up geometry

// Transition
float transitionTimer = 0.0f;
const float TRANSITION_DURATION = 2.0f; // 2 seconds transition
bool transitionStarted = false;
float menuSoulStartY = 350.0f; // Starting Y position of menu soul
float menuSoulTargetY = 100.0f; // Target Y position for game start
float menuSoulCurrentY = 350.0f;

// --- CONSTANTS ---
const float SCROLL_THRESHOLD = 300.0f;
const float GRAVITY = 0.6f;
const float JUMP_FORCE = 15.0f;
const float MOVE_SPEED = 7.0f;
const float FALL_MULTIPLIER = 1.2f;
const float COYOTE_LIMIT = 15.0f;
const float MAX_JUMP_REACH = 300.0f;
const int MAX_PLATFORMS_PER_ROW = 3; // Maximum platforms that can spawn in a row
const float MIN_PLATFORM_SPACING = 150.0f; // Minimum space between platforms in same row
const float SCREEN_WRAP_OFFSET = 30.0f; // How far off-screen before wrapping

// NEW: Power-up constants
const float POWERUP_SPAWN_CHANCE = 0.05f; // 5% chance when spawning platform
const float SPEED_BOOST_DURATION = 0.5f;    // Short duration for visual effect
const float FLY_UPWARD_FORCE = 50.0f;    // Strong upward force for 20m flight
const float FLY_DISTANCE = 20.0f * 50.0f; // 20 meters in pixels (since 1m = 50 pixels)
const float SHIELD_DURATION = 6.0f;
const float SLOW_TIME_DURATION = 5.0f;
const float MAGNET_DURATION = 10.0f;
const float MAGNET_RANGE = 150.0f;
const float SLOW_TIME_FACTOR = 0.5f; // Boss moves at 50% speed

// NEW: Boss laser constants
const float LASER_DURATION = 5.0f;     // How long laser stays active
const float LASER_SPEED = 50.0f;        // Speed laser moves upward
const float LASER_WIDTH = 20.0f;       // Width of laser beam
const float STUN_DURATION = 1.5f;      // How long player is stunned
const float ATTACK_WARNING_DURATION = 3.0f; // Warning before attack
const int LASERS_PER_ATTACK = 5;       // Number of lasers per attack

// --- COLORS ---
const float C_PLAT_TOP[] = { 0.7f, 0.75f, 0.8f };
const float C_PLAT_SIDE[] = { 0.4f, 0.45f, 0.5f };
const float C_PLAT_DECO[] = { 0.3f, 0.35f, 0.4f };
const float C_CRACK_DARK[] = { 0.15f, 0.1f, 0.1f };
const float C_DROP_CORE[] = { 1.0f, 1.0f, 1.0f, 1.0f };
const float C_DROP_CYAN[] = { 0.2f, 0.9f, 1.0f, 0.9f };
const float C_DROP_BLUE[] = { 0.1f, 0.4f, 0.9f, 0.5f };
const float C_DROP_HALO[] = { 0.0f, 0.3f, 0.8f, 0.2f };

// Power-up colors
const float C_POWERUP_SPEED[] = { 1.0f, 0.8f, 0.2f };     // Gold/Yellow
const float C_POWERUP_JUMP[] = { 0.2f, 1.0f, 0.3f };      // Green
const float C_POWERUP_SHIELD[] = { 0.2f, 0.6f, 1.0f };    // Blue
const float C_POWERUP_SLOW[] = { 0.8f, 0.2f, 1.0f };      // Purple
const float C_POWERUP_MAGNET[] = { 1.0f, 0.3f, 0.3f };    // Red

// Power-up glow colors
const float C_GLOW_SPEED[] = { 1.0f, 0.9f, 0.3f, 0.5f };
const float C_GLOW_JUMP[] = { 0.3f, 1.0f, 0.4f, 0.5f };
const float C_GLOW_SHIELD[] = { 0.3f, 0.7f, 1.0f, 0.5f };
const float C_GLOW_SLOW[] = { 0.9f, 0.3f, 1.0f, 0.5f };
const float C_GLOW_MAGNET[] = { 1.0f, 0.4f, 0.4f, 0.5f };

// NEW: Laser colors
const float C_LASER_CORE[] = { 1.0f, 0.2f, 0.1f, 1.0f };     // Bright red core
const float C_LASER_GLOW[] = { 1.0f, 0.5f, 0.1f, 0.7f };     // Orange glow
const float C_LASER_OUTER[] = { 1.0f, 0.8f, 0.3f, 0.3f };    // Yellow outer glow
const float C_WARNING_GLOW[] = { 1.0f, 0.3f, 0.1f, 0.5f };   // Attack warning glow

// Boss Colors
const float C_FUR_RED[] = { 0.55f, 0.05f, 0.05f };
const float C_SKIN_BLUE[] = { 0.5f, 0.55f, 0.7f };
const float C_WING_PURP[] = { 0.3f, 0.0f, 0.5f };
const float C_WING_BONE[] = { 0.1f, 0.1f, 0.1f };
const float C_CLAW_RED[] = { 0.9f, 0.1f, 0.1f };
const float C_HAIR_BLACK[] = { 0.1f, 0.1f, 0.1f };
const float C_HORN_GREY[] = { 0.8f, 0.8f, 0.8f };
const float C_METAL[] = { 0.2f, 0.2f, 0.2f };
const float C_STAR[] = { 1.0f, 0.7f, 0.0f };
const float C_SHADOW[] = { 0.15f, 0.15f, 0.15f };
const float C_WING_PURP_LIGHT[] = { 0.45f, 0.05f, 0.7f };
const float C_FACE_SHADOW[] = { 0.35f, 0.4f, 0.55f };
const float C_HORN_DARK[] = { 0.65f, 0.65f, 0.65f };
const float C_SKIN_HI[] = { 0.65f, 0.72f, 0.88f };
const float C_FUR_SHADOW[] = { 0.4f, 0.02f, 0.02f };
const float C_METAL_HI[] = { 0.55f, 0.55f, 0.55f };
const float C_CLAW_SHADOW[] = { 0.6f, 0.06f, 0.06f };

// NEW: Stun effect color
const float C_STUN_EFFECT[] = { 1.0f, 1.0f, 0.5f, 0.3f }; // Yellowish white for stun

//Background
float hellGlowPulse = 0.0f;  // Controls pulsing glow for lava, cracks, and top spikes animation

// ==========================================
// 2. HELPER FUNCTIONS
// ==========================================

void renderBitmapString(float x, float y, void* font, const char* string)
{
    const char* c;
    glRasterPos2f(x, y);
    for (c = string; *c != '\0'; c++)
    {
        glutBitmapCharacter(font, *c);
    }
}

int getPlatformsPerRow()
{
    float chance = (rand() % 100) / 100.0f;
    if (chance < CHANCE_2_PLATFORMS)
        return 2;
    else if (chance < CHANCE_2_PLATFORMS + CHANCE_3_PLATFORMS)
        return 3;
    return 1;
}

// NEW: Helper function to handle screen wrapping safely
void handleScreenWrapping()
{
    // Check if player is completely off-screen (beyond wrap threshold)
    if (soul.x < -100 || soul.x > 900)
    {
        // Reset horizontal velocity to prevent infinite wrapping loops
        soul.velocityX *= 0.5f;

        // Ensure we wrap to a reasonable position
        if (soul.x < -100)
        {
            soul.x = 850; // Wrap to right side
        }
        else if (soul.x > 900)
        {
            soul.x = -50; // Wrap to left side
        }
    }
}

void DrawTri(float x1, float y1, float x2, float y2, float x3, float y3, const float* color)
{
    glColor3fv(color);
    glBegin(GL_TRIANGLES);
    glVertex2f(x1, y1); glVertex2f(x2, y2); glVertex2f(x3, y3);
    glEnd();
}

void DrawQuad(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, const float* color)
{
    glColor3fv(color);
    glBegin(GL_QUADS);
    glVertex2f(x1, y1); glVertex2f(x2, y2); glVertex2f(x3, y3); glVertex2f(x4, y4);
    glEnd();
}

// NEW: Draw a fire laser
void DrawFireLaser(float x, float y, float width, float height, float chargeProgress = 0.0f, bool isWarning = false, float warningProgress = 0.0f)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    if (isWarning)
    {
        // Simple warning beam (symbols are drawn separately)
        float pulse = sin(g_Time * 12.0f) * 0.3f + 0.7f;
        float intensity = warningProgress * 1.2f;

        // Transparent warning beam
        glColor4f(1.0f, 0.3f, 0.1f, 0.2f * pulse * intensity);
        glBegin(GL_QUADS);
        glVertex2f(-width / 2 - 10, -height * warningProgress);
        glVertex2f(width / 2 + 10, -height * warningProgress);
        glVertex2f(width / 2 + 10, 0);
        glVertex2f(-width / 2 - 10, 0);
        glEnd();
    }
    else
    {
        // ACTUAL LASER - EXTREME SPEED VISUAL
        // Outer trail (wider for speed effect)
        glColor4f(1.0f, 0.8f, 0.3f, 0.4f);
        glBegin(GL_QUADS);
        glVertex2f(-width / 2 - 25, -height);
        glVertex2f(width / 2 + 25, -height);
        glVertex2f(width / 2 + 25, 0);
        glVertex2f(-width / 2 - 25, 0);
        glEnd();

        // Middle glow (intense)
        float middlePulse = sin(g_Time * 25.0f) * 0.2f + 0.8f;
        glColor4f(1.0f, 0.5f, 0.1f, 0.7f * middlePulse);
        glBegin(GL_QUADS);
        glVertex2f(-width / 2 - 12, -height);
        glVertex2f(width / 2 + 12, -height);
        glVertex2f(width / 2 + 12, 0);
        glVertex2f(-width / 2 - 12, 0);
        glEnd();

        // Core beam (EXTREMELY BRIGHT for speed)
        float corePulse = sin(g_Time * 30.0f) * 0.3f + 0.7f;
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f * corePulse);
        glBegin(GL_QUADS);
        glVertex2f(-width / 2, -height);
        glVertex2f(width / 2, -height);
        glVertex2f(width / 2, 0);
        glVertex2f(-width / 2, 0);
        glEnd();

        // Speed lines effect
        for (int i = 0; i < 8; i++)
        {
            float lineX = (i - 4) * (width / 8);
            float lineAlpha = 0.6f - (i * 0.08f);

            glColor4f(1.0f, 0.9f, 0.4f, lineAlpha);
            glBegin(GL_LINES);
            glVertex2f(lineX, 0);
            glVertex2f(lineX + (rand() % 20 - 10), -height);
            glEnd();
        }
    }

    glPopMatrix();
}

// NEW: Draw stun effect around player
void DrawStunEffect(float x, float y, float alpha)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    // Outer stun ring
    glColor4f(1.0f, 1.0f, 0.5f, alpha * 0.3f);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        float radius = 60.0f + sin(g_Time * 8.0f) * 5.0f;
        glVertex2f(cos(angle) * radius, sin(angle) * radius);
    }
    glEnd();

    // Inner stun effect
    glColor4f(1.0f, 1.0f, 0.8f, alpha * 0.5f);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 16; i++)
    {
        float angle = i * 2.0f * 3.14159f / 16.0f;
        float radius = 40.0f + sin(g_Time * 10.0f) * 3.0f;
        glVertex2f(cos(angle) * radius, sin(angle) * radius);
    }
    glEnd();

    // Electricity/spark effects
    glColor4f(1.0f, 1.0f, 0.2f, alpha);
    for (int spark = 0; spark < 4; spark++)
    {
        float angle = spark * 3.14159f / 2.0f + g_Time * 5.0f;
        float length = 30.0f + sin(g_Time * 15.0f + spark) * 10.0f;

        glBegin(GL_LINE_STRIP);
        for (int i = 0; i < 5; i++)
        {
            float progress = i / 4.0f;
            float offset = (rand() % 10 - 5) * 0.1f;
            glVertex2f(cos(angle + offset) * length * progress,
                sin(angle + offset) * length * progress);
        }
        glEnd();
    }

    glPopMatrix();
}

// NEW: Draw warning symbol at laser positions
void DrawWarningSymbol(float x, float y, float progress, float size = 50.0f) // Increased size
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    // Calculate distance from player horizontally (not vertically)
    float dx = x - soul.x;
    float horizontalDistance = abs(dx);

    // Make symbol more intense when player is horizontally close
    float intensity = 1.0f;
    if (horizontalDistance < 100.0f) intensity = 1.5f;
    else if (horizontalDistance < 200.0f) intensity = 1.2f;

    // Pulsing effect gets faster as warning time runs out
    float pulseSpeed = 10.0f + progress * 10.0f;
    float pulse = sin(g_Time * pulseSpeed) * 0.4f + 0.6f;
    float scale = size * (1.0f + progress * 0.8f) * pulse * intensity;

    // Draw connecting line from symbol to where laser will start
    float laserStartY = bossY + 50;
    float lineAlpha = 0.3f + sin(g_Time * 8.0f) * 0.2f;
    glColor4f(1.0f, 0.3f, 0.1f, lineAlpha);
    glBegin(GL_LINES);
    glVertex2f(0, scale * 1.5f); // Top of symbol
    glVertex2f(0, laserStartY);  // To boss position
    glEnd();

    // Outer warning circle (bright red)
    glColor4f(1.0f, 0.1f, 0.1f, 0.7f * pulse);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        glVertex2f(cos(angle) * scale, sin(angle) * scale);
    }
    glEnd();

    // Middle warning circle (orange)
    glColor4f(1.0f, 0.6f, 0.1f, 0.9f * pulse);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 24; i++)
    {
        float angle = i * 2.0f * 3.14159f / 24.0f;
        glVertex2f(cos(angle) * (scale * 0.7f), sin(angle) * (scale * 0.7f));
    }
    glEnd();

    // Danger symbol (!) - bigger and brighter
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    // Exclamation mark stem
    glBegin(GL_QUADS);
    glVertex2f(-scale * 0.1f, -scale * 0.5f);
    glVertex2f(scale * 0.1f, -scale * 0.5f);
    glVertex2f(scale * 0.1f, scale * 0.4f);
    glVertex2f(-scale * 0.1f, scale * 0.4f);
    glEnd();

    // Exclamation mark dot
    glBegin(GL_QUADS);
    glVertex2f(-scale * 0.15f, scale * 0.5f);
    glVertex2f(scale * 0.15f, scale * 0.5f);
    glVertex2f(scale * 0.15f, scale * 0.7f);
    glVertex2f(-scale * 0.15f, scale * 0.7f);
    glEnd();

    // Rotating warning triangles
    glPushMatrix();
    glRotatef(g_Time * 120.0f, 0.0f, 0.0f, 1.0f);

    for (int i = 0; i < 4; i++) // 4 triangles for more visibility
    {
        glPushMatrix();
        glRotatef(i * 90.0f, 0.0f, 0.0f, 1.0f);

        glColor4f(1.0f, 0.0f, 0.0f, 0.8f);
        glBegin(GL_TRIANGLES);
        glVertex2f(0.0f, scale * 1.3f);
        glVertex2f(-scale * 0.4f, scale * 0.8f);
        glVertex2f(scale * 0.4f, scale * 0.8f);
        glEnd();

        glPopMatrix();
    }

    glPopMatrix();

    glPopMatrix();
}

void DrawPolyDrop(float w, float h, float wave, const float* cCenter, const float* cEdge)
{
    glBegin(GL_TRIANGLE_FAN);
    glColor4fv(cCenter);
    glVertex2f(0.0f, -0.1f);
    glColor4fv(cEdge);
    glVertex2f(0.0f + wave, h);
    glVertex2f(-w * 0.5f + (wave * 0.5f), h * 0.4f);
    glVertex2f(-w, 0.0f);
    glVertex2f(-w * 0.6f, -h * 0.4f);
    glVertex2f(0.0f, -h * 0.6f);
    glVertex2f(w * 0.6f, -h * 0.4f);
    glVertex2f(w, 0.0f);
    glVertex2f(w * 0.5f + (wave * 0.5f), h * 0.4f);
    glVertex2f(0.0f + wave, h);
    glEnd();
}

// NEW: Draw different power-up types
void DrawPowerUp(float x, float y, PowerUpType type, float rotation, float glow, float scale = 1.0f)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);
    glRotatef(rotation, 0.0f, 0.0f, 1.0f);

    const float* color;
    const float* glowColor;

    // Set colors based on power-up type
    switch (type)
    {
    case POWERUP_SPEED_BOOST:
        color = C_POWERUP_SPEED;
        glowColor = C_GLOW_SPEED;
        break;
    case POWERUP_DOUBLE_JUMP:
        color = C_POWERUP_JUMP;
        glowColor = C_GLOW_JUMP;
        break;
    case POWERUP_SHIELD:
        color = C_POWERUP_SHIELD;
        glowColor = C_GLOW_SHIELD;
        break;
    case POWERUP_SLOW_TIME:
        color = C_POWERUP_SLOW;
        glowColor = C_GLOW_SLOW;
        break;
    case POWERUP_MAGNET:
        color = C_POWERUP_MAGNET;
        glowColor = C_GLOW_MAGNET;
        break;
    default:
        color = C_POWERUP_SPEED;
        glowColor = C_GLOW_SPEED;
    }

    // Draw glow effect
    glPushMatrix();
    float pulse = sin(g_Time * 8.0f) * 0.1f + 1.0f;
    glScalef(pulse * glow, pulse * glow, 1.0f);
    glColor4fv(glowColor);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        glVertex2f(cos(angle) * 25.0f, sin(angle) * 25.0f);
    }
    glEnd();
    glPopMatrix();

    // Draw power-up shape based on type
    glColor3fv(color);

    switch (type)
    {
    case POWERUP_SPEED_BOOST: // Lightning bolt shape
        glBegin(GL_TRIANGLES);
        glVertex2f(-10.0f, 20.0f); glVertex2f(10.0f, 0.0f); glVertex2f(-5.0f, 0.0f);
        glVertex2f(-5.0f, 0.0f); glVertex2f(10.0f, 0.0f); glVertex2f(-10.0f, -20.0f);
        glVertex2f(-10.0f, -20.0f); glVertex2f(10.0f, 0.0f); glVertex2f(5.0f, -10.0f);
        glEnd();
        break;

    case POWERUP_DOUBLE_JUMP: // Two overlapping arrows
        glBegin(GL_TRIANGLES);
        // First arrow
        glVertex2f(-15.0f, 0.0f); glVertex2f(0.0f, 15.0f); glVertex2f(0.0f, -15.0f);
        // Second arrow (slightly offset)
        glVertex2f(-8.0f, 0.0f); glVertex2f(7.0f, 15.0f); glVertex2f(7.0f, -15.0f);
        glEnd();
        break;

    case POWERUP_SHIELD: // Shield shape
        glBegin(GL_TRIANGLE_FAN);
        for (int i = 0; i <= 16; i++)
        {
            float angle = 3.14159f + i * 3.14159f / 16.0f;
            glVertex2f(cos(angle) * 20.0f, sin(angle) * 15.0f);
        }
        glEnd();
        // Cross on shield
        glColor3f(1.0f, 1.0f, 1.0f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        glVertex2f(-8.0f, 0.0f); glVertex2f(8.0f, 0.0f);
        glVertex2f(0.0f, -8.0f); glVertex2f(0.0f, 8.0f);
        glEnd();
        break;

    case POWERUP_SLOW_TIME: // Hourglass shape
        glBegin(GL_QUADS);
        glVertex2f(-12.0f, 20.0f); glVertex2f(12.0f, 20.0f);
        glVertex2f(5.0f, 0.0f); glVertex2f(-5.0f, 0.0f);
        glVertex2f(-5.0f, 0.0f); glVertex2f(5.0f, 0.0f);
        glVertex2f(12.0f, -20.0f); glVertex2f(-12.0f, -20.0f);
        glEnd();
        break;

    case POWERUP_MAGNET: // Magnet shape
        glBegin(GL_QUADS);
        glVertex2f(-20.0f, 10.0f); glVertex2f(20.0f, 10.0f);
        glVertex2f(20.0f, -10.0f); glVertex2f(-20.0f, -10.0f);
        glEnd();
        // Magnet poles
        glColor3f(0.5f, 0.5f, 0.5f);
        glBegin(GL_QUADS);
        glVertex2f(-20.0f, 10.0f); glVertex2f(-15.0f, 10.0f);
        glVertex2f(-15.0f, -10.0f); glVertex2f(-20.0f, -10.0f);
        glVertex2f(20.0f, 10.0f); glVertex2f(15.0f, 10.0f);
        glVertex2f(15.0f, -10.0f); glVertex2f(20.0f, -10.0f);
        glEnd();
        break;
    }

    glPopMatrix();
}

// Visual effect for the fly upward power-up
void DrawFlyEffect(float x, float y, float alpha)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    // Create wing-like effects that move up and down
    for (int side = -1; side <= 1; side += 2)
    {
        glPushMatrix();

        // Position wings vertically (one above, one below)
        float verticalOffset = side * 30.0f;

        // Add up-down oscillation
        float oscillation = sin(g_Time * 15.0f + side) * 8.0f;
        glTranslatef(0.0f, verticalOffset + oscillation, 0.0f);

        // Wing shape - now horizontal wings
        glColor4f(1.0f, 0.8f, 0.2f, alpha * 0.6f);
        glBegin(GL_TRIANGLE_FAN);
        // Draw a horizontal wing shape
        glVertex2f(0.0f, 0.0f); // Center
        for (int i = 0; i <= 8; i++)
        {
            float angle = i * 3.14159f / 4.0f; // Quarter circle increments
            float radiusX = 25.0f + sin(g_Time * 18.0f + side) * 3.0f; // Horizontal size with pulse
            float radiusY = 8.0f; // Vertical size (smaller for wing shape)
            glVertex2f(cos(angle) * radiusX, sin(angle) * radiusY);
        }
        glEnd();

        // Wing trail - vertical trails since wings are horizontal
        glColor4f(1.0f, 0.9f, 0.3f, alpha * 0.4f);
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i < 6; i++)
        {
            float offset = i * -6.0f; // Trail goes upward
            float width = (6 - i) * 3.0f;
            glVertex2f(-width, offset);
            glVertex2f(width, offset);
        }
        glEnd();

        glPopMatrix();
    }

    // Add central glow effect
    glColor4f(1.0f, 0.9f, 0.3f, alpha * 0.3f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(0.0f, 0.0f);
    for (int i = 0; i <= 16; i++)
    {
        float angle = i * 2.0f * 3.14159f / 16.0f;
        float radius = 15.0f + sin(g_Time * 12.0f) * 3.0f;
        glVertex2f(cos(angle) * radius, sin(angle) * radius);
    }
    glEnd();

    // Add upward energy streams
    glColor4f(1.0f, 0.8f, 0.2f, alpha * 0.5f);
    for (int stream = 0; stream < 3; stream++)
    {
        float streamX = (stream - 1) * 12.0f; // -12, 0, 12
        float streamLength = 40.0f + sin(g_Time * 10.0f + stream) * 10.0f;

        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= 4; i++)
        {
            float progress = i / 4.0f;
            float yPos = -progress * streamLength;
            float width = 4.0f * (1.0f - progress * 0.5f); // Taper
            glVertex2f(streamX - width, yPos);
            glVertex2f(streamX + width, yPos);
        }
        glEnd();
    }

    glPopMatrix();
}

// NEW: Draw player shield effect
void DrawShieldEffect(float x, float y, float alpha)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    // Outer shield
    glColor4f(0.2f, 0.6f, 1.0f, alpha * 0.3f);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        glVertex2f(cos(angle) * 45.0f, sin(angle) * 45.0f);
    }
    glEnd();

    // Inner shield with pulse
    float pulse = sin(g_Time * 10.0f) * 0.1f + 0.9f;
    glColor4f(0.4f, 0.8f, 1.0f, alpha * 0.6f);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        glVertex2f(cos(angle) * 35.0f * pulse, sin(angle) * 35.0f * pulse);
    }
    glEnd();

    glPopMatrix();
}

// NEW: Draw speed trail effect
void DrawSpeedTrail(float x, float y, float alpha)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    glColor4f(1.0f, 0.9f, 0.3f, alpha);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < 5; i++)
    {
        float offset = i * -10.0f;
        float width = (5 - i) * 8.0f;
        glVertex2f(offset, -width);
        glVertex2f(offset, width);
    }
    glEnd();

    glPopMatrix();
}

// NEW: Draw magnet effect field
void DrawMagnetField(float x, float y, float range)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    // Magnetic field lines
    glColor4f(1.0f, 0.3f, 0.3f, 0.2f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        glVertex2f(cos(angle) * range, sin(angle) * range);
    }
    glEnd();

    // Pulsing inner circle
    float pulse = sin(g_Time * 12.0f) * 0.2f + 0.8f;
    glColor4f(1.0f, 0.4f, 0.4f, 0.1f);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        glVertex2f(cos(angle) * range * 0.3f * pulse, sin(angle) * range * 0.3f * pulse);
    }
    glEnd();

    glPopMatrix();
}

// OPTIMIZATION: This function is now used to CREATE the Display List once.
// It draws a "Unit" platform (Width = 1.0) which we scale later.
void CompilePlatformGeometry()
{
    platformDisplayList = glGenLists(1);
    glNewList(platformDisplayList, GL_COMPILE);

    // Draw standard platform geometry normalized to width 1.0
    // We assume width is 1.0 and height is 1.0 in this model space
    DrawQuad(-0.5f, 0.1f, 0.5f, 0.1f, 0.45f, -0.15f, -0.45f, -0.15f, C_PLAT_SIDE);
    DrawQuad(-0.5f, 0.15f, 0.5f, 0.15f, 0.5f, 0.1f, -0.5f, 0.1f, C_PLAT_TOP);
    DrawQuad(-0.4f, -0.15f, -0.3f, -0.15f, -0.32f, -0.25f, -0.38f, -0.25f, C_PLAT_DECO);
    DrawQuad(0.3f, -0.15f, 0.4f, -0.15f, 0.38f, -0.25f, 0.32f, -0.25f, C_PLAT_DECO);
    DrawQuad(-0.2f, -0.15f, 0.2f, -0.15f, 0.15f, -0.2f, -0.15f, -0.2f, C_PLAT_DECO);

    glEndList();
}

// Breakable platforms are still drawn normally because they change too much (cracks/shaking)
void DrawPlatformBreakable(float x, float y, float pixelWidth, float pixelScale)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(pixelWidth, pixelScale, 1.0f);
    glPushMatrix();
    glTranslatef(-0.3f, 0.0f, 0.0f);
    DrawQuad(-0.2f, 0.15f, 0.2f, 0.12f, 0.18f, 0.08f, -0.2f, 0.1f, C_PLAT_TOP);
    DrawQuad(-0.2f, 0.1f, 0.18f, 0.08f, 0.15f, -0.15f, -0.18f, -0.12f, C_PLAT_SIDE);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0.0f, -0.02f, 0.0f);
    DrawQuad(-0.1f, 0.12f, 0.1f, 0.14f, 0.08f, 0.09f, -0.08f, 0.07f, C_PLAT_TOP);
    DrawQuad(-0.1f, 0.07f, 0.1f, 0.09f, 0.05f, -0.18f, -0.05f, -0.16f, C_PLAT_SIDE);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0.3f, 0.01f, 0.0f);
    DrawQuad(-0.2f, 0.13f, 0.2f, 0.15f, 0.2f, 0.1f, -0.18f, 0.09f, C_PLAT_TOP);
    DrawQuad(-0.18f, 0.09f, 0.2f, 0.1f, 0.18f, -0.13f, -0.15f, -0.16f, C_PLAT_SIDE);
    glPopMatrix();
    DrawTri(-0.12f, 0.15f, -0.08f, 0.15f, -0.1f, 0.0f, C_CRACK_DARK);
    DrawTri(0.1f, 0.0f, 0.15f, -0.1f, 0.05f, -0.2f, C_CRACK_DARK);
    glPopMatrix();
}

void DrawSpirit(float x, float y, float scale, float stretchX, float stretchY)
{
    // NEW: Apply invisibility alpha
    float baseAlpha = soul.isInvisible ? soul.invisibilityAlpha : 1.0f;

    glPushMatrix();
    glTranslatef(x, y, 0.0f);
    glScalef(scale * stretchX, scale * stretchY, 1.0f);

    float hover = sin(g_Time * 1.5f) * 0.08f;
    glTranslatef(0.0f, hover, 0.0f);
    float tipWave = sin(g_Time * 10.0f) * 0.15f;

    // Halo
    glPushMatrix();
    glScalef(1.1f, 1.1f, 1.0f);
    glRotatef(g_Time * 30.0f, 0.0f, 0.0f, 1.0f);
    glColor4f(C_DROP_HALO[0], C_DROP_HALO[1], C_DROP_HALO[2], C_DROP_HALO[3] * baseAlpha * 0.7f);
    glBegin(GL_POLYGON);
    for (int i = 0; i < 8; i++)
    {
        float a = i * 6.28f / 8.0f;
        glVertex2f(cos(a) * 0.6f, sin(a) * 0.6f);
    }
    glEnd();
    glPopMatrix();

    // Apply alpha to all drawing functions
    float cyan[4] = { C_DROP_CYAN[0], C_DROP_CYAN[1], C_DROP_CYAN[2], C_DROP_CYAN[3] * baseAlpha };
    float blue[4] = { C_DROP_BLUE[0], C_DROP_BLUE[1], C_DROP_BLUE[2], C_DROP_BLUE[3] * baseAlpha };
    DrawPolyDrop(0.4f, 0.8f, tipWave, cyan, blue);

    glPushMatrix();
    glTranslatef(0.0f, -0.1f, 0.0f);
    glScalef(0.5f, 0.5f, 1.0f);
    float core[4] = { C_DROP_CORE[0], C_DROP_CORE[1], C_DROP_CORE[2], C_DROP_CORE[3] * baseAlpha };
    float cyan2[4] = { C_DROP_CYAN[0], C_DROP_CYAN[1], C_DROP_CYAN[2], C_DROP_CYAN[3] * baseAlpha };
    DrawPolyDrop(0.4f, 0.8f, tipWave * 0.5f, core, cyan2);
    glPopMatrix();

    // Bubbles
    for (int i = 0; i < 3; i++)
    {
        glPushMatrix();
        float offset = i * 2.0f;
        float bubbleY = fmod(g_Time * 2.0f + offset, 1.5f) - 0.5f;
        float bubbleX = sin(bubbleY * 3.0f + offset) * 0.2f;
        glTranslatef(bubbleX, bubbleY, 0.0f);
        glScalef(0.1f, 0.1f, 1.0f);
        glColor4f(C_DROP_CYAN[0], C_DROP_CYAN[1], C_DROP_CYAN[2], C_DROP_CYAN[3] * baseAlpha * 0.5f);
        glBegin(GL_QUADS);
        glVertex2f(0.0f, 1.0f); glVertex2f(0.8f, 0.0f);
        glVertex2f(0.0f, -1.0f); glVertex2f(-0.8f, 0.0f);
        glEnd();
        glPopMatrix();
    }
    glPopMatrix();
}

void DrawBoss(float x, float y, float scale)
{
    glPushMatrix();
    glTranslatef(0.0f, 0.1f, 0.0f);
    glTranslatef(x, y, 0.0f);
    glScalef(scale, scale, 1.0f);

    float flapAngle = sin(g_Time * 15.0f) * 25.0f;

    // Wings
    for (int i = 0; i < 2; i++)
    {
        glPushMatrix();
        if (i == 1) glScalef(-1.0f, 1.0f, 1.0f);
        glTranslatef(0.55f, 0.4f, 0.0f);
        glRotatef(flapAngle, 0.0f, 0.0f, 1.0f);
        glTranslatef(-0.55f, -0.4f, 0.0f);

        glColor3fv(C_WING_PURP);
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(0.2f, 0.4f); glVertex2f(1.1f, 0.8f); glVertex2f(0.9f, 0.4f);
        glVertex2f(1.0f, 0.0f); glVertex2f(0.6f, -0.2f); glVertex2f(0.7f, -0.5f); glVertex2f(0.2f, -0.4f);
        glEnd();
        // (Simplified Boss parts for brevity, keeping all your logic)
        glColor3fv(C_WING_PURP_LIGHT);
        glBegin(GL_POLYGON);
        glVertex2f(0.28f, 0.38f); glVertex2f(0.95f, 0.72f); glVertex2f(0.78f, 0.38f);
        glVertex2f(0.9f, 0.05f); glVertex2f(0.55f, -0.12f); glVertex2f(0.63f, -0.35f); glVertex2f(0.3f, -0.32f);
        glEnd();
        glLineWidth(3.0f);
        glColor3fv(C_WING_BONE);
        glBegin(GL_LINES);
        glVertex2f(0.2f, 0.4f); glVertex2f(1.1f, 0.8f);
        glVertex2f(0.2f, 0.4f); glVertex2f(1.0f, 0.0f);
        glVertex2f(0.2f, 0.4f); glVertex2f(0.7f, -0.5f);
        glVertex2f(0.55f, 0.55f); glVertex2f(0.7f, 0.05f);
        glVertex2f(0.75f, 0.3f); glVertex2f(0.62f, -0.22f);
        glEnd();
        DrawTri(1.05f, 0.75f, 1.15f, 0.85f, 1.0f, 0.9f, C_CLAW_RED);
        DrawTri(0.9f, 0.7f, 1.0f, 0.75f, 0.95f, 0.85f, C_CLAW_RED);
        glPopMatrix();
    }

    // Legs
    for (int i = 0; i < 2; i++)
    {
        glPushMatrix();
        if (i == 1)
        {
            glScalef(-1.0f, 1.0f, 1.0f);
        }
        glTranslatef(0.25f, -0.4f, 0.0f);
        glColor3fv(C_FUR_RED);
        glBegin(GL_POLYGON);
        glVertex2f(-0.15f, 0.1f); glVertex2f(0.15f, 0.1f); glVertex2f(0.2f, -0.1f);
        glVertex2f(0.15f, -0.2f); glVertex2f(0.18f, -0.3f); glVertex2f(0.05f, -0.4f); glVertex2f(-0.1f, -0.3f);
        glEnd();
        DrawTri(0.15f, -0.1f, 0.22f, -0.15f, 0.15f, -0.2f, C_FUR_RED);
        DrawTri(0.12f, -0.25f, 0.18f, -0.3f, 0.1f, -0.35f, C_FUR_RED);
        glColor3fv(C_FUR_SHADOW);
        glBegin(GL_TRIANGLES);
        glVertex2f(-0.05f, 0.1f); glVertex2f(0.12f, -0.05f); glVertex2f(-0.02f, -0.22f);
        glEnd();
        glColor3fv(C_SKIN_BLUE);
        glBegin(GL_POLYGON);
        glVertex2f(-0.06f, -0.32f); glVertex2f(0.12f, -0.34f); glVertex2f(0.1f, -0.55f);
        glVertex2f(0.05f, -0.6f); glVertex2f(-0.08f, -0.58f);
        glEnd();
        DrawQuad(-0.03f, -0.55f, 0.09f, -0.55f, 0.09f, -0.62f, -0.03f, -0.62f, C_METAL);
        DrawTri(0.0f, -0.58f, 0.02f, -0.58f, 0.01f, -0.65f, C_CLAW_RED);
        glTranslatef(0.03f, -0.62f, 0.0f);
        DrawQuad(-0.07f, 0.02f, 0.07f, 0.02f, 0.1f, -0.18f, -0.1f, -0.18f, C_SKIN_BLUE);
        DrawTri(-0.11f, -0.1f, -0.05f, -0.1f, -0.16f, -0.28f, C_CLAW_RED);
        DrawTri(0.05f, -0.1f, 0.11f, -0.1f, 0.1f, -0.22f, C_CLAW_SHADOW);
        glPopMatrix();
    }

    // Body
    glColor3fv(C_FUR_RED);
    glBegin(GL_POLYGON);
    glVertex2f(-0.35f, 0.45f); glVertex2f(0.35f, 0.45f); glVertex2f(0.25f, -0.1f); glVertex2f(-0.25f, -0.1f);
    glEnd();
    glColor3fv(C_HAIR_BLACK);
    glBegin(GL_TRIANGLE_STRIP);
    glVertex2f(-0.35f, 0.45f); glVertex2f(-0.3f, 0.5f);
    glVertex2f(-0.1f, 0.46f); glVertex2f(-0.15f, 0.55f);
    glVertex2f(0.1f, 0.46f); glVertex2f(0.15f, 0.55f);
    glVertex2f(0.35f, 0.45f); glVertex2f(0.3f, 0.5f);
    glEnd();
    glColor3fv(C_SKIN_BLUE);
    glBegin(GL_POLYGON);
    glVertex2f(-0.25f, 0.4f); glVertex2f(0.25f, 0.4f); glVertex2f(0.1f, 0.1f);
    glVertex2f(0.0f, 0.0f); glVertex2f(-0.1f, 0.1f);
    glEnd();
    glColor3fv(C_STAR);
    glBegin(GL_TRIANGLES);
    glVertex2f(0.0f, 0.36f); glVertex2f(0.14f, 0.14f); glVertex2f(-0.14f, 0.14f);
    glEnd();
    glColor3fv(C_FACE_SHADOW);
    glBegin(GL_POLYGON);
    glVertex2f(-0.12f, 0.32f); glVertex2f(0.12f, 0.32f); glVertex2f(0.05f, 0.16f); glVertex2f(-0.05f, 0.16f);
    glEnd();
    glColor3fv(C_SKIN_HI);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.06f, 0.24f); glVertex2f(0.0f, 0.3f); glVertex2f(0.06f, 0.24f);
    glEnd();
    DrawQuad(-0.26f, -0.1f, 0.26f, -0.1f, 0.24f, -0.2f, -0.24f, -0.2f, C_METAL);
    glColor3fv(C_HORN_GREY);
    glBegin(GL_POLYGON);
    glVertex2f(-0.05f, -0.12f); glVertex2f(0.05f, -0.12f);
    glVertex2f(0.07f, -0.15f); glVertex2f(0.05f, -0.18f);
    glVertex2f(-0.05f, -0.18f); glVertex2f(-0.07f, -0.15f);
    glEnd();
    DrawQuad(-0.14f, -0.2f, 0.14f, -0.2f, 0.14f, -0.34f, -0.14f, -0.34f, C_FUR_RED);

    // Head/Hair
    glPushMatrix();
    glTranslatef(0.0f, 0.5f, 0.0f);
    glColor3fv(C_HAIR_BLACK);
    DrawTri(-0.24f, 0.08f, -0.12f, 0.22f, -0.32f, 0.32f, C_HAIR_BLACK);
    DrawTri(-0.12f, 0.2f, 0.0f, 0.26f, -0.16f, 0.42f, C_HAIR_BLACK);
    DrawTri(0.12f, 0.2f, 0.0f, 0.26f, 0.16f, 0.42f, C_HAIR_BLACK);
    DrawTri(0.08f, 0.26f, 0.02f, 0.32f, 0.12f, 0.5f, C_HAIR_BLACK);
    glColor3fv(C_SKIN_BLUE);
    glBegin(GL_POLYGON);
    glVertex2f(-0.15f, 0.2f); glVertex2f(0.15f, 0.2f); glVertex2f(0.15f, 0.0f);
    glVertex2f(0.0f, -0.15f); glVertex2f(-0.15f, 0.0f);
    glEnd();
    glColor3fv(C_FACE_SHADOW);
    glBegin(GL_POLYGON);
    glVertex2f(-0.14f, 0.14f); glVertex2f(0.14f, 0.14f); glVertex2f(0.1f, 0.02f);
    glVertex2f(0.0f, -0.1f); glVertex2f(-0.1f, 0.02f);
    glEnd();
    DrawTri(-0.13f, 0.2f, -0.03f, 0.2f, -0.17f, 0.54f, C_HAIR_BLACK);
    DrawTri(-0.12f, 0.2f, -0.05f, 0.2f, -0.16f, 0.52f, C_HORN_GREY);
    DrawTri(-0.05f, 0.2f, -0.16f, 0.52f, -0.08f, 0.52f, C_HORN_DARK);
    DrawTri(0.13f, 0.2f, 0.03f, 0.2f, 0.17f, 0.54f, C_HAIR_BLACK);
    DrawTri(0.12f, 0.2f, 0.05f, 0.2f, 0.16f, 0.52f, C_HORN_GREY);
    DrawTri(0.05f, 0.2f, 0.16f, 0.52f, 0.08f, 0.52f, C_HORN_DARK);
    glColor3f(1.0f, 1.0f, 1.0f);
    DrawTri(-0.05f, -0.01f, -0.02f, -0.01f, -0.035f, -0.07f, C_HORN_GREY);
    DrawTri(0.05f, -0.01f, 0.02f, -0.01f, 0.035f, -0.07f, C_HORN_GREY);
    glPopMatrix();

    // Arms
    for (int i = 0; i < 2; i++)
    {
        glPushMatrix();
        if (i == 1) glScalef(-1.0f, 1.0f, 1.0f);
        glTranslatef(0.45f, 0.1f, 0.0f);
        glColor3fv(C_FUR_RED);
        glBegin(GL_POLYGON);
        glVertex2f(-0.12f, 0.22f); glVertex2f(0.12f, 0.18f); glVertex2f(0.18f, -0.02f);
        glVertex2f(0.0f, -0.12f); glVertex2f(-0.18f, 0.02f);
        glEnd();
        DrawTri(0.1f, 0.18f, 0.22f, 0.12f, 0.14f, 0.0f, C_FUR_SHADOW);
        DrawQuad(-0.09f, 0.02f, 0.09f, -0.02f, 0.07f, -0.32f, -0.07f, -0.32f, C_FUR_RED);
        glTranslatef(0.0f, -0.35f, 0.0f);
        DrawQuad(-0.08f, 0.05f, 0.08f, 0.05f, 0.07f, -0.05f, -0.07f, -0.05f, C_METAL);
        glTranslatef(0.0f, -0.1f, 0.0f);
        DrawQuad(-0.1f, 0.05f, 0.1f, 0.05f, 0.08f, -0.15f, -0.08f, -0.15f, C_SKIN_BLUE);
        glColor3fv(C_SKIN_HI);
        glBegin(GL_TRIANGLES);
        glVertex2f(-0.06f, 0.02f); glVertex2f(0.06f, 0.02f); glVertex2f(0.0f, -0.1f);
        glEnd();
        DrawTri(-0.1f, 0.0f, -0.08f, -0.05f, -0.15f, -0.1f, C_CLAW_RED);
        DrawTri(0.04f, -0.15f, 0.08f, -0.15f, 0.07f, -0.28f, C_CLAW_RED);
        glPopMatrix();
    }
    glPopMatrix();
}

// NEW: Draw invisibility effect around player
void DrawInvisibilityEffect(float x, float y, float alpha)
{
    glPushMatrix();
    glTranslatef(x, y, 0.0f);

    // Outer shimmering ring
    float ringPulse = sin(g_Time * 12.0f) * 0.3f + 0.7f;
    glColor4f(0.9f, 0.9f, 1.0f, alpha * 0.2f * ringPulse);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 32; i++)
    {
        float angle = i * 2.0f * 3.14159f / 32.0f;
        float radius = 60.0f + sin(g_Time * 8.0f + i) * 5.0f;
        glVertex2f(cos(angle) * radius, sin(angle) * radius);
    }
    glEnd();

    // Inner distortion field
    glColor4f(0.7f, 0.7f, 1.0f, alpha * 0.1f);
    glBegin(GL_TRIANGLE_FAN);
    for (int i = 0; i <= 24; i++)
    {
        float angle = i * 2.0f * 3.14159f / 24.0f;
        float radius = 45.0f + sin(g_Time * 15.0f + i * 2.0f) * 3.0f;
        glVertex2f(cos(angle) * radius, sin(angle) * radius);
    }
    glEnd();

    glPopMatrix();
}

// NEW: Apply invisibility effect to player
void applyInvisibility(float duration = 3.0f)
{
    soul.invisibilityTimer = duration;
    soul.isInvisible = true;
    soul.invisibilityAlpha = 0.3f; // Semi-transparent during invisibility

    // Create invisibility activation particles
    for (int i = 0; i < 30; i++)
    {
        Particle p;
        p.x = soul.x;
        p.y = soul.y;
        p.alpha = 0.8f;
        p.size = 4.0f + (rand() % 10);
        p.velocityX = (rand() % 200 - 100) * 0.1f;
        p.velocityY = (rand() % 200 - 100) * 0.1f;
        p.color[0] = 0.9f; p.color[1] = 0.9f; p.color[2] = 1.0f; // Light blue/white particles
        particles.push_back(p);
    }
}

// NEW: Highscore file functions
const char* HIGHSCORE_FILE = "soulscape_highscore.txt";

// Save highscore to file
void saveHighscore()
{
    std::ofstream file(HIGHSCORE_FILE);
    if (file.is_open())
    {
        file << bestDistance;
        file.close();
        std::cout << "Highscore saved: " << bestDistance << " m" << std::endl;
    }
    else
    {
        std::cout << "Error: Could not save highscore to file!" << std::endl;
    }
}

// Load highscore from file
void loadHighscore()
{
    std::ifstream file(HIGHSCORE_FILE);
    if (file.is_open())
    {
        std::string line;
        if (std::getline(file, line))
        {
            std::stringstream ss(line);
            ss >> bestDistance;
        }
        file.close();
        std::cout << "Highscore loaded: " << bestDistance << " m" << std::endl;
    }
    else
    {
        // If file doesn't exist, create it with default value 0
        std::cout << "Highscore file not found. Creating new file." << std::endl;
        bestDistance = 0.0f;
        saveHighscore();
    }
}

// Update highscore if needed and save it
void updateAndSaveHighscore(float currentDistance)
{
    if (currentDistance > bestDistance)
    {
        bestDistance = currentDistance;
        saveHighscore();
        std::cout << "New highscore! " << bestDistance << " m" << std::endl;
    }
}

// ==========================================
// 3. Power ups function
// ==========================================
// NEW: Create a power-up
void spawnPowerUp(float x, float y)
{
    PowerUp p;
    p.x = x;
    p.y = y;
    p.velocityY = 0;
    p.active = true;
    p.rotation = 0.0f;
    p.bobOffset = (rand() % 100) * 0.01f * 3.14159f * 2.0f;
    p.glowIntensity = 1.0f;

    // Randomly select power-up type
    int type = rand() % 5;
    switch (type)
    {
    case 0: p.type = POWERUP_SPEED_BOOST; break;
    case 1: p.type = POWERUP_DOUBLE_JUMP; break;
    case 2: p.type = POWERUP_SHIELD; break;
    case 3: p.type = POWERUP_SLOW_TIME; break;
    case 4: p.type = POWERUP_MAGNET; break;
    default: p.type = POWERUP_SPEED_BOOST;
    }

    powerUps.push_back(p);
}

// NEW: Apply power-up effect to player
void applyPowerUp(PowerUpType type)
{
    // PLAY SOUND IMMEDIATELY
    switch (type)
    {
    case POWERUP_SPEED_BOOST:
        SoundSystem::getInstance()->playPowerUpSpeed();
        break;
    case POWERUP_DOUBLE_JUMP:
        SoundSystem::getInstance()->playPowerUpDoubleJump();
        break;
    case POWERUP_SHIELD:
        SoundSystem::getInstance()->playPowerUpShield();
        break;
    }

    switch (type)
    {
    case POWERUP_SPEED_BOOST:
        soul.speedBoostTimer = SPEED_BOOST_DURATION;
        // NEW: Apply strong upward force for flying
        soul.velocityY = FLY_UPWARD_FORCE;

        if (soul.velocityY < FLY_UPWARD_FORCE)
        {
            soul.velocityY = FLY_UPWARD_FORCE;
        }

        soul.isGrounded = false;
        soul.isJumping = true;
        soul.coyoteTimer = 0;
        break;

    case POWERUP_DOUBLE_JUMP:
        soul.hasDoubleJump = true;
        soul.doubleJumpUsed = false;
        break;

    case POWERUP_SHIELD:
        soul.shieldTimer = SHIELD_DURATION;
        soul.shieldAlpha = 1.0f;
        break;

    case POWERUP_SLOW_TIME:
        globalSlowFactor = SLOW_TIME_FACTOR;
        slowTimeEffectTimer = SLOW_TIME_DURATION;
        break;

    case POWERUP_MAGNET:
        soul.hasMagnet = true;
        soul.magnetTimer = MAGNET_DURATION;
        magnetRange = MAGNET_RANGE;
        break;
    }
}

// NEW: Update power-ups
void updatePowerUps()
{
    // Remove expired power-ups
    for (int i = powerUps.size() - 1; i >= 0; i--)
    {

        // Update active power-ups
        if (powerUps[i].active)
        {
            // Bobbing motion
            powerUps[i].y += sin(g_Time * 2.0f + powerUps[i].bobOffset) * 0.5f;
            powerUps[i].rotation += 2.0f;

            // Check collision with player
            float dx = powerUps[i].x - soul.x;
            float dy = powerUps[i].y - soul.y;
            float distance = sqrt(dx * dx + dy * dy);

            if (distance < 30.0f) // Player collected power-up
            {
                powerUps[i].active = false;
                applyPowerUp(powerUps[i].type);

                // Create collection particles
                for (int j = 0; j < 20; j++)
                {
                    Particle part;
                    part.x = powerUps[i].x;
                    part.y = powerUps[i].y;
                    part.alpha = 1.0f;
                    part.size = 3.0f + (rand() % 10) * 0.5f;
                    part.velocityX = (rand() % 100 - 50) * 0.1f;
                    part.velocityY = (rand() % 100 - 50) * 0.1f;

                    // Set color based on power-up type
                    switch (powerUps[i].type)
                    {
                    case POWERUP_SPEED_BOOST:
                        part.color[0] = 1.0f; part.color[1] = 0.8f; part.color[2] = 0.2f;
                        break;
                    case POWERUP_DOUBLE_JUMP:
                        part.color[0] = 0.2f; part.color[1] = 1.0f; part.color[2] = 0.3f;
                        break;
                    case POWERUP_SHIELD:
                        part.color[0] = 0.2f; part.color[1] = 0.6f; part.color[2] = 1.0f;
                        break;
                    case POWERUP_SLOW_TIME:
                        part.color[0] = 0.8f; part.color[1] = 0.2f; part.color[2] = 1.0f;
                        break;
                    case POWERUP_MAGNET:
                        part.color[0] = 1.0f; part.color[1] = 0.3f; part.color[2] = 0.3f;
                        break;
                    }
                    particles.push_back(part);
                }

                // Remove the collected power-up immediately
                powerUps.erase(powerUps.begin() + i);
            }
            // Magnet attraction if player has magnet
            else if (soul.hasMagnet && distance < magnetRange && distance > 10.0f)
            {
                float force = 5.0f;
                powerUps[i].x -= dx / distance * force;
                powerUps[i].y -= dy / distance * force;
            }
        }
    }
}

// NEW: Update player power-up timers
void updatePlayerPowerUps()
{
    // Update speed boost
    if (soul.speedBoostTimer > 0)
    {
        soul.speedBoostTimer -= 0.016f; // Assuming 60fps
        soul.speedTrailTimer += 0.2f;

        //Create flying particles
        if (rand() % 3 == 0) // Less frequent particles
        {
            Particle p;
            p.x = soul.x + (rand() % 20 - 10);
            p.y = soul.y - 20;
            p.alpha = 1.0f;
            p.size = 4.0f + (rand() % 6);
            p.velocityX = (rand() % 100 - 50) * 0.05f;
            p.velocityY = -3.0f - (rand() % 5); // Particles fall downward while player flies up
            p.color[0] = 1.0f; p.color[1] = 0.8f; p.color[2] = 0.2f;
            particles.push_back(p);
        }

        if (soul.speedBoostTimer <= 0)
        {
            soul.speedBoostTimer = 0;
        }
    }

    // Update shield
    if (soul.shieldTimer > 0)
    {
        soul.shieldTimer -= 0.016f;
        soul.shieldAlpha = soul.shieldTimer / SHIELD_DURATION;
        if (soul.shieldTimer <= 0)
        {
            soul.shieldTimer = 0;
            soul.shieldAlpha = 0.0f;
        }
    }

    // Update slow time effect
    if (slowTimeEffectTimer > 0)
    {
        slowTimeEffectTimer -= 0.016f;
        if (slowTimeEffectTimer <= 0)
        {
            slowTimeEffectTimer = 0;
            globalSlowFactor = 1.0f;
        }
    }

    // Update magnet
    if (soul.hasMagnet)
    {
        soul.magnetTimer -= 0.016f;
        if (soul.magnetTimer <= 0)
        {
            soul.hasMagnet = false;
            magnetRange = 0.0f;
        }
    }

    // Reset double jump when grounded
    if (soul.isGrounded)
    {
        soul.doubleJumpUsed = false;
    }

    // NEW: Update invisibility
    if (soul.isInvisible)
    {
        soul.invisibilityTimer -= 0.016f;

        // Pulsing effect during invisibility
        soul.invisibilityAlpha = 0.3f + sin(g_Time * 10.0f) * 0.1f;

        // Create occasional invisibility particles
        if (rand() % 10 == 0)
        {
            Particle p;
            p.x = soul.x + (rand() % 40 - 20);
            p.y = soul.y + (rand() % 40 - 20);
            p.alpha = 0.6f;
            p.size = 3.0f + (rand() % 6);
            p.velocityX = (rand() % 100 - 50) * 0.05f;
            p.velocityY = (rand() % 100 - 50) * 0.05f;
            p.color[0] = 0.9f; p.color[1] = 0.9f; p.color[2] = 1.0f;
            particles.push_back(p);
        }

        // End invisibility when timer expires
        if (soul.invisibilityTimer <= 0)
        {
            soul.isInvisible = false;
            soul.invisibilityTimer = 0;
            soul.invisibilityAlpha = 1.0f;

            // Create invisibility end effect
            for (int i = 0; i < 20; i++)
            {
                Particle p;
                p.x = soul.x;
                p.y = soul.y;
                p.alpha = 0.9f;
                p.size = 5.0f + (rand() % 8);
                p.velocityX = (rand() % 200 - 100) * 0.1f;
                p.velocityY = (rand() % 200 - 100) * 0.1f;
                p.color[0] = 1.0f; p.color[1] = 1.0f; p.color[2] = 1.0f; // White particles
                particles.push_back(p);
            }
        }
    }
}

// ==========================================
// NEW: BOSS LASER ATTACK FUNCTIONS
// ==========================================

// Create a new fire laser attack
void spawnFireLaser(float x, bool isWarning = false)
{
    FireLaser laser;
    laser.x = x;

    // Set Y position based on type
    if (isWarning)
    {
        laser.y = 50.0f; // FIXED BOTTOM POSITION for warning symbols
        laser.speed = 0.0f;

        // PLAY LASER WARNING SOUND IMMEDIATELY
        SoundSystem::getInstance()->playLaserWarningSound();
    }
    else
    {
        laser.y = bossY + 50; // Start from boss for actual lasers
        laser.speed = LASER_SPEED;

        // PLAY LASER FIRE SOUND IMMEDIATELY
        SoundSystem::getInstance()->playLaserFireSound();
    }

    laser.active = true;
    laser.width = isWarning ? LASER_WIDTH * 1.5f : LASER_WIDTH; // Bigger warning
    laser.timer = isWarning ? ATTACK_WARNING_DURATION : LASER_DURATION;
    laser.maxTimer = laser.timer;
    laser.chargeTimer = 0.0f;
    laser.glowIntensity = 1.0f;
    laser.isWarning = isWarning;      // SET whether this is warning
    laser.warningProgress = 0.0f;     // Initialize warning progress
    laser.symbolScale = 1.0f;  // ADD THIS

    fireLasers.push_back(laser);
}

// Update all fire lasers
void updateFireLasers()
{
    // Update boss attack cooldown
    if (bossAttackCooldown > 0)
    {
        bossAttackCooldown -= 0.016f;
    }

    // Update attack warning
    if (bossAttackWarningTimer > 0)
    {
        bossAttackWarningTimer -= 0.016f;

        // Spawn lasers when warning finishes
        if (bossAttackWarningTimer <= 0)
        {
            // Spawn multiple lasers at different positions
            for (int i = 0; i < LASERS_PER_ATTACK; i++)
            {
                float laserX;
                if (i == 0)
                {
                    // First laser targets player position
                    laserX = bossAttackWarningX;
                }
                else
                {
                    // Other lasers at random positions
                    laserX = 100 + (rand() % 600); // Between 100 and 700
                }
                spawnFireLaser(laserX);
            }

            // Reset warning
            bossAttackWarningTimer = 0;
            bossAttackWarningX = 0;
        }
    }

    // Update existing lasers
    for (int i = fireLasers.size() - 1; i >= 0; i--)
    {
        if (fireLasers[i].active)
        {
            // For WARNING lasers
            if (fireLasers[i].isWarning)
            {
                // Update warning progress (0 to 1)
                fireLasers[i].warningProgress = 1.0f - (fireLasers[i].timer / ATTACK_WARNING_DURATION);

                // FIXED: Always keep warning symbol at BOTTOM of screen
                // Position it near the bottom (around y = 150-200 pixels)
                fireLasers[i].y = 50.0f; // Fixed position at bottom

                // Make warning symbol pulse more intensely as time runs out
                float pulseSpeed = 8.0f + fireLasers[i].warningProgress * 8.0f;
                fireLasers[i].glowIntensity = 0.8f + sin(g_Time * pulseSpeed) * 0.2f;
                fireLasers[i].symbolScale = 1.0f + sin(g_Time * pulseSpeed) * (0.3f + fireLasers[i].warningProgress * 0.4f);

                // Add particles at fixed bottom position
                if (rand() % 5 == 0)
                {
                    Particle p;
                    p.x = fireLasers[i].x + (rand() % 60 - 30);
                    p.y = fireLasers[i].y + (rand() % 30);
                    p.alpha = 0.9f;
                    p.size = 3.0f + (rand() % 8);
                    p.velocityX = (rand() % 100 - 50) * 0.05f;
                    p.velocityY = 1.0f + (rand() % 3); // Particles float upward slowly
                    p.color[0] = 1.0f; p.color[1] = 0.3f; p.color[2] = 0.1f;
                    particles.push_back(p);
                }
            }
            else // For ACTUAL lasers
            {
                // Move laser upward VERY FAST
                fireLasers[i].y += fireLasers[i].speed * globalSlowFactor;

                // Actual lasers disappear quickly
                fireLasers[i].timer -= 0.016f;
            }

            // Update timers for both types
            fireLasers[i].timer -= 0.016f;

            // Check collision with player (only if fully charged)
            if (!fireLasers[i].isWarning && fireLasers[i].timer > 0)
            {
                float playerLeft = soul.x - 15;
                float playerRight = soul.x + 15;
                float playerTop = soul.y + 40;
                float playerBottom = soul.y;

                float laserLeft = fireLasers[i].x - fireLasers[i].width / 2;
                float laserRight = fireLasers[i].x + fireLasers[i].width / 2;
                float laserTop = fireLasers[i].y;
                float laserBottom = fireLasers[i].y - 600; // Laser extends to top of screen

                // Check collision
                if (playerRight > laserLeft && playerLeft < laserRight &&
                    playerBottom < laserTop && playerTop > laserBottom)
                {
                    // Check if player has shield
                    if (soul.shieldTimer > 0)
                    {
                        // NEW: Shield activates invisibility instead of breaking
                        applyInvisibility(3.0f); // 3 seconds of invisibility

                        // Create shield activation effect (not breaking)
                        for (int j = 0; j < 15; j++)
                        {
                            Particle p;
                            p.x = soul.x;
                            p.y = soul.y;
                            p.alpha = 1.0f;
                            p.size = 4.0f + (rand() % 10);
                            p.velocityX = (rand() % 200 - 100) * 0.1f;
                            p.velocityY = (rand() % 200 - 100) * 0.1f;
                            p.color[0] = 0.2f; p.color[1] = 0.6f; p.color[2] = 1.0f;
                            particles.push_back(p);
                        }

                        // NOTE: Shield timer continues normally - doesn't break
                        // The shield still provides its normal duration protection
                    }
                    else if (!soul.isStunned)
                    {
                        // Stun the player
                        soul.isStunned = true;
                        soul.stunTimer = STUN_DURATION;

                        SoundSystem::getInstance()->playHurtSound();

                        // Create stun effect particles
                        for (int j = 0; j < 25; j++)
                        {
                            Particle p;
                            p.x = soul.x;
                            p.y = soul.y;
                            p.alpha = 1.0f;
                            p.size = 2.0f + (rand() % 6);
                            p.velocityX = (rand() % 200 - 100) * 0.1f;
                            p.velocityY = (rand() % 200 - 100) * 0.1f;
                            p.color[0] = 1.0f; p.color[1] = 1.0f; p.color[2] = 0.5f;
                            particles.push_back(p);
                        }

                        // Reduce laser intensity (player absorbed some energy)
                        fireLasers[i].glowIntensity *= 0.7f;
                    }
                }
            }

            // Check if laser has expired
            if (fireLasers[i].timer <= 0)
            {
                fireLasers[i].active = false;
                fireLasers.erase(fireLasers.begin() + i);
                continue;
            }
        }
        else
        {
            // Remove inactive lasers
            fireLasers.erase(fireLasers.begin() + i);
        }
    }
}

// Update player stun status
void updatePlayerStun()
{
    if (soul.isStunned)
    {
        soul.stunTimer -= 0.016f;

        // Reduce player movement while stunned
        soul.velocityX *= 0.9f;
        soul.velocityY *= 0.9f;

        // Create stun particles occasionally
        if (rand() % 5 == 0)
        {
            Particle p;
            p.x = soul.x + (rand() % 40 - 20);
            p.y = soul.y + (rand() % 40 - 20);
            p.alpha = 0.8f;
            p.size = 2.0f + (rand() % 4);
            p.velocityX = (rand() % 100 - 50) * 0.05f;
            p.velocityY = (rand() % 100 - 50) * 0.05f;
            p.color[0] = 1.0f; p.color[1] = 1.0f; p.color[2] = 0.5f;
            particles.push_back(p);
        }

        // End stun when timer expires
        if (soul.stunTimer <= 0)
        {
            soul.isStunned = false;
            soul.stunTimer = 0;
        }
    }
}

// Boss decides when to attack
// REPLACE the entire updateBossAttack function with:
void updateBossAttack()
{
    // Update warning phase
    if (isWarningPhase)
    {
        warningPhaseTimer -= 0.016f;

        // Create warning symbols for each laser position
        for (int i = 0; i < fireLasers.size(); i++)
        {
            if (fireLasers[i].isWarning)
            {
                // Update warning progress
                fireLasers[i].warningProgress = 1.0f - (fireLasers[i].timer / ATTACK_WARNING_DURATION);
            }
        }

        // End warning phase and shoot lasers
        if (warningPhaseTimer <= 0)
        {
            isWarningPhase = false;

            // Remove all warning lasers
            for (int i = fireLasers.size() - 1; i >= 0; i--)
            {
                if (fireLasers[i].isWarning)
                {
                    fireLasers.erase(fireLasers.begin() + i);
                }
            }

            // Spawn actual FAST lasers at warning positions
            for (float x : laserWarningPositions)
            {
                spawnFireLaser(x, false); // Actual laser
            }

            // Clear warning positions
            laserWarningPositions.clear();

            // Start cooldown
            bossAttackCooldown = BOSS_ATTACK_COOLDOWN_MAX;
        }
    }
    else if (bossAttackCooldown <= 0 && soul.y > bossY - 200 && currentState == PLAYING)
    {
        // Chance to start an attack
        float attackChance = 0.02f;
        attackChance += (totalScore / 50000.0f) * 0.01f;

        if ((rand() % 1000) / 1000.0f < attackChance)
        {
            // Start WARNING PHASE
            isWarningPhase = true;
            warningPhaseTimer = ATTACK_WARNING_DURATION;

            // Clear any previous warnings
            laserWarningPositions.clear();

            // Create warning lasers at random positions
            for (int i = 0; i < LASERS_PER_ATTACK; i++)
            {
                float laserX;
                if (i == 0)
                {
                    // First laser targets player position
                    laserX = soul.x + soul.velocityX * 15.0f; // Predict further ahead
                }
                else
                {
                    // Other lasers at random positions
                    laserX = 100 + (rand() % 600);
                }

                // Clamp to screen bounds
                if (laserX < 100) laserX = 100;
                if (laserX > 700) laserX = 700;

                // Store warning position
                laserWarningPositions.push_back(laserX);

                // Spawn warning laser
                spawnFireLaser(laserX, true);
            }
        }
    }
}

// ==========================================
// 4. MENU DRAWING
// ==========================================

void drawStartMenu()
{
    // Only draw full menu if not in transition
    if (currentState != TRANSITION)
    {
        // Draw the menu soul at its static position
        DrawSpirit(400.0f, 350.0f, 80.0f, 1.0f, 1.0f);

        char title[] = "S O U L S C A P E";
        char sub[] = "The Infinite Climb";

        glColor3f(0.2f, 0.9f, 1.0f);
        renderBitmapString(310.0f, 450.0f, GLUT_BITMAP_TIMES_ROMAN_24, title);

        glColor3f(0.6f, 0.6f, 0.6f);
        renderBitmapString(340.0f, 420.0f, GLUT_BITMAP_HELVETICA_12, sub);

        // Show different text based on exit dialog state
        if (showExitDialog)
        {
            // Draw exit confirmation dialog
            drawExitConfirmation();
        }
        else
        {
            // Normal menu instructions
            glColor3f(1.0f, 1.0f, 1.0f);
            renderBitmapString(300.0f, 200.0f, GLUT_BITMAP_HELVETICA_18, "Press [ENTER] to Start");

            glColor3f(0.8f, 0.8f, 0.8f);
            renderBitmapString(290.0f, 150.0f, GLUT_BITMAP_HELVETICA_12, "Controls: W to Jump, A / D to Move");
            renderBitmapString(300.0f, 130.0f, GLUT_BITMAP_HELVETICA_12, "Avoid the rising demon below...");
            renderBitmapString(320.0f, 110.0f, GLUT_BITMAP_HELVETICA_12, "Press ESC to exit");
        }

        // ==========================================
        // POWER-UP INSTRUCTIONS IN LEFT BOTTOM
        // ==========================================

        // Draw background for instructions (semi-transparent panel)
        glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
        glBegin(GL_QUADS);
        glVertex2f(10.0f, 10.0f);
        glVertex2f(160.0f, 10.0f);
        glVertex2f(160.0f, 180.0f);
        glVertex2f(10.0f, 180.0f);
        glEnd();

        // Draw border for instructions panel
        glColor3f(0.3f, 0.7f, 1.0f);
        glLineWidth(2.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(10.0f, 10.0f);
        glVertex2f(160.0f, 10.0f);
        glVertex2f(160.0f, 180.0f);
        glVertex2f(10.0f, 180.0f);
        glEnd();
        glLineWidth(1.0f);

        // Title for power-ups section
        glColor3f(0.2f, 0.9f, 1.0f);
        renderBitmapString(20.0f, 160.0f, GLUT_BITMAP_HELVETICA_12, "POWER-UPS:");

        float startY = 140.0f;
        float iconX = 25.0f;
        float textX = 65.0f;
        float lineSpacing = 25.0f;

        // Speed Boost
        glPushMatrix();
        glTranslatef(iconX, startY, 0.0f);
        DrawPowerUp(0, 0, POWERUP_SPEED_BOOST, 0, 1.0f, 0.5f);
        glPopMatrix();
        glColor3f(1.0f, 0.8f, 0.2f);
        renderBitmapString(textX, startY - 5.0f, GLUT_BITMAP_HELVETICA_12, "Fly Up");
        startY -= lineSpacing;

        // Double Jump
        glPushMatrix();
        glTranslatef(iconX, startY, 0.0f);
        DrawPowerUp(0, 0, POWERUP_DOUBLE_JUMP, 0, 1.0f, 0.5f);
        glPopMatrix();
        glColor3f(0.2f, 1.0f, 0.3f);
        renderBitmapString(textX, startY - 5.0f, GLUT_BITMAP_HELVETICA_12, "Double Jump");
        startY -= lineSpacing;

        // Shield
        glPushMatrix();
        glTranslatef(iconX, startY, 0.0f);
        DrawPowerUp(0, 0, POWERUP_SHIELD, 0, 1.0f, 0.5f);
        glPopMatrix();
        glColor3f(0.2f, 0.6f, 1.0f);
        renderBitmapString(textX, startY - 5.0f, GLUT_BITMAP_HELVETICA_12, "Shield");
        startY -= lineSpacing;

        // Slow Time
        glPushMatrix();
        glTranslatef(iconX, startY, 0.0f);
        DrawPowerUp(0, 0, POWERUP_SLOW_TIME, 0, 1.0f, 0.5f);
        glPopMatrix();
        glColor3f(0.8f, 0.2f, 1.0f);
        renderBitmapString(textX, startY - 5.0f, GLUT_BITMAP_HELVETICA_12, "Slow Time");
        startY -= lineSpacing;

        // Magnet
        glPushMatrix();
        glTranslatef(iconX, startY, 0.0f);
        DrawPowerUp(0, 0, POWERUP_MAGNET, 0, 1.0f, 0.5f);
        glPopMatrix();
        glColor3f(1.0f, 0.3f, 0.3f);
        renderBitmapString(textX, startY - 5.0f, GLUT_BITMAP_HELVETICA_12, "Magnet");

        // Draw separator line above instructions
        glColor3f(0.3f, 0.7f, 1.0f);
        glBegin(GL_LINES);
        glVertex2f(10.0f, 185.0f);
        glVertex2f(160.0f, 185.0f);
        glEnd();
    }
    else
    {
        // In transition state: Only draw the falling soul and platforms
        // The soul falls from menu position to game position
        DrawSpirit(400.0f, menuSoulCurrentY, 80.0f, 1.0f, 1.0f);


    }
}

void updateTransition()
{
    if (currentState == TRANSITION)
    {
        transitionTimer += 0.016f; // Assuming 60fps

        // Calculate falling progress (0 to 1)
        float progress = transitionTimer / TRANSITION_DURATION;

        if (progress > 1.0f) progress = 1.0f;

        // Ease-out falling animation (starts fast, ends slow)
        float easedProgress = 1.0f - pow(1.0f - progress, 3.0f);

        // Update soul position during transition
        menuSoulCurrentY = menuSoulStartY - (menuSoulStartY - menuSoulTargetY) * easedProgress;

        // Add gravity effect to the falling
        float fallSpeed = 0.5f + easedProgress * 2.0f;
        menuSoulCurrentY -= fallSpeed;

        // Create falling particles
        if (rand() % 3 == 0)
        {
            Particle p;
            p.x = 400.0f + (rand() % 20 - 10);
            p.y = menuSoulCurrentY + 40.0f;
            p.alpha = 0.7f;
            p.size = 3.0f + (rand() % 6);
            p.velocityX = (rand() % 100 - 50) * 0.05f;
            p.velocityY = -2.0f - (rand() % 3); // Particles fall with soul
            p.color[0] = 0.2f; p.color[1] = 0.9f; p.color[2] = 1.0f;
            particles.push_back(p);
        }

        // When transition is complete
        if (transitionTimer >= TRANSITION_DURATION)
        {
            initializeGamePlatforms();

            // Reset player to starting position
            soul.x = 400;
            soul.y = 100;
            soul.velocityY = 0;
            soul.coyoteTimer = 0;
            soul.isGrounded = false;

            // Reset transition variables
            transitionTimer = 0.0f;
            transitionStarted = false;
            menuSoulCurrentY = menuSoulStartY;

            // Start the game
            currentState = PLAYING;

            // Create landing effect particles
            for (int i = 0; i < 20; i++)
            {
                Particle p;
                p.x = soul.x + (rand() % 40 - 20);
                p.y = soul.y;
                p.alpha = 1.0f;
                p.size = 4.0f + (rand() % 8);
                p.velocityX = (rand() % 200 - 100) * 0.1f;
                p.velocityY = (rand() % 100) * 0.1f + 2.0f;
                p.color[0] = 0.2f; p.color[1] = 0.9f; p.color[2] = 1.0f;
                particles.push_back(p);
            }
        }
    }
}

void drawGameOverMenu()
{
    DrawBoss(400.0f, 310.0f, 100.0f);

    glColor3f(1.0f, 0.2f, 0.2f);
    renderBitmapString(330.0f, 500.0f, GLUT_BITMAP_TIMES_ROMAN_24, "S O U L   L O S T");

    float distance = totalScore / 50.0f;
    char buffer[50];

    if (distance >= bestDistance && distance > 1.0f)
    {
        glColor3f(1.0f, 0.8f, 0.0f);
        sprintf_s(buffer, sizeof(buffer), "NEW RECORD: %.1f m", distance);
        renderBitmapString(320.0f, 450.0f, GLUT_BITMAP_HELVETICA_18, buffer);
    }
    else
    {
        glColor3f(1.0f, 1.0f, 1.0f);
        sprintf_s(buffer, sizeof(buffer), "Distance Travelled: %.1f m", distance);
        renderBitmapString(300.0f, 450.0f, GLUT_BITMAP_HELVETICA_18, buffer);
    }

    glColor3f(0.6f, 0.6f, 0.6f);
    renderBitmapString(280.0f, 150.0f, GLUT_BITMAP_HELVETICA_18, "Press [R] to Restart");
    renderBitmapString(310.0f, 120.0f, GLUT_BITMAP_HELVETICA_12, "Press [M] for Menu");
}

void drawPauseMenu()
{
    // Darken the game background
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f * pauseMenuAlpha);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(800, 0);
    glVertex2f(800, 600);
    glVertex2f(0, 600);
    glEnd();

    // Draw pause menu panel
    float panelX = 250.0f;
    float panelY = 200.0f;
    float panelWidth = 300.0f;
    float panelHeight = 300.0f;

    // Panel background
    glColor4f(0.15f, 0.15f, 0.2f, 0.95f * pauseMenuAlpha);
    glBegin(GL_QUADS);
    glVertex2f(panelX, panelY);
    glVertex2f(panelX + panelWidth, panelY);
    glVertex2f(panelX + panelWidth, panelY + panelHeight);
    glVertex2f(panelX, panelY + panelHeight);
    glEnd();

    // Panel border
    glColor4f(0.2f, 0.9f, 1.0f, 1.0f * pauseMenuAlpha);
    glLineWidth(3.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panelX, panelY);
    glVertex2f(panelX + panelWidth, panelY);
    glVertex2f(panelX + panelWidth, panelY + panelHeight);
    glVertex2f(panelX, panelY + panelHeight);
    glEnd();
    glLineWidth(1.0f);

    // Title
    glColor3f(1.0f, 1.0f, 1.0f);
    renderBitmapString(panelX + 100.0f, panelY + 250.0f, GLUT_BITMAP_TIMES_ROMAN_24, "PAUSED");

    // Menu options
    const char* options[] = { "Resume", "Restart Game", "Back to Main Menu" };
    float optionY = panelY + 180.0f;

    for (int i = 0; i < PAUSE_OPTIONS_COUNT; i++)
    {
        // Highlight selected option
        if (i == pauseMenuSelection)
        {
            glColor3f(0.2f, 0.9f, 1.0f);
            // Draw selection indicator
            glBegin(GL_QUADS);
            glVertex2f(panelX + 40.0f, optionY - 5.0f);
            glVertex2f(panelX + panelWidth - 40.0f, optionY - 5.0f);
            glVertex2f(panelX + panelWidth - 40.0f, optionY + 15.0f);
            glVertex2f(panelX + 40.0f, optionY + 15.0f);
            glEnd();

            glColor3f(0.0f, 0.0f, 0.0f); // Black text for selected
        }
        else
        {
            glColor3f(0.8f, 0.8f, 0.8f);
        }

        // Center text
        float textWidth = glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)options[i]);
        float textX = panelX + (panelWidth - textWidth) / 2.0f;

        renderBitmapString(textX, optionY, GLUT_BITMAP_HELVETICA_18, options[i]);
        optionY -= 50.0f;
    }

    // Instructions
    glColor3f(0.6f, 0.6f, 0.6f);
    renderBitmapString(panelX + 80.0f, panelY + 30.0f, GLUT_BITMAP_HELVETICA_12, "Use UP/DOWN to navigate");
    renderBitmapString(panelX + 100.0f, panelY + 10.0f, GLUT_BITMAP_HELVETICA_12, "ENTER to select");
}

void drawExitConfirmation()
{
    // Draw semi-transparent overlay
    glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(800, 0);
    glVertex2f(800, 600);
    glVertex2f(0, 600);
    glEnd();

    // Draw confirmation box
    float boxWidth = 400.0f;
    float boxHeight = 150.0f;
    float boxX = 200.0f;
    float boxY = 225.0f;

    // Box background
    glColor3f(0.2f, 0.2f, 0.25f);
    glBegin(GL_QUADS);
    glVertex2f(boxX, boxY);
    glVertex2f(boxX + boxWidth, boxY);
    glVertex2f(boxX + boxWidth, boxY + boxHeight);
    glVertex2f(boxX, boxY + boxHeight);
    glEnd();

    // Box border
    glColor3f(1.0f, 0.3f, 0.3f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(boxX, boxY);
    glVertex2f(boxX + boxWidth, boxY);
    glVertex2f(boxX + boxWidth, boxY + boxHeight);
    glVertex2f(boxX, boxY + boxHeight);
    glEnd();
    glLineWidth(1.0f);

    // Title
    glColor3f(1.0f, 0.5f, 0.5f);
    renderBitmapString(boxX + 160.0f, boxY + 120.0f, GLUT_BITMAP_HELVETICA_18, "Exit Game?");

    // Message
    glColor3f(1.0f, 1.0f, 1.0f);
    renderBitmapString(boxX + 130.0f, boxY + 90.0f, GLUT_BITMAP_HELVETICA_12, "Are you sure you want to exit?");

    // Instructions
    glColor3f(0.8f, 0.8f, 0.8f);
    renderBitmapString(boxX + 140.0f, boxY + 60.0f, GLUT_BITMAP_HELVETICA_12, "Press ENTER to confirm");
    renderBitmapString(boxX + 140.0f, boxY + 40.0f, GLUT_BITMAP_HELVETICA_12, "Press ESC to cancel");
}

// ==========================================
// 5. GAME LOGIC (OPTIMIZED)
// ==========================================

// REPLACEMENT for spawnPlatform: Recycles an existing platform
void recyclePlatform(Platform& p, float y, int rowPlatformCount = 1, int platformIndex = 0, bool forceNewPosition = false)
{
    p.y = y;
    p.active = true;
    p.steppedOn = false;

    // Reset Properties
    if (rand() % 5 == 0)
    {
        p.isBreakable = true;
        p.breakTimer = 15.0f;
        p.width = 90.0f;
    }
    else
    {
        p.isBreakable = false;
        p.breakTimer = 0;
        p.width = (rand() % 150) + 80;
    }

    float newX;
    float halfWidth = p.width / 2.0f;

    if (forceNewPosition || platformIndex > 0)
    {
        // For multiple platforms in same row, distribute across screen
        float screenWidth = 700.0f; // 50 to 750
        float sectionWidth = screenWidth / rowPlatformCount;

        // Calculate this platform's section
        float sectionLeft = 50 + (sectionWidth * platformIndex);
        float sectionRight = sectionLeft + sectionWidth;

        // Ensure platform fits within section
        if (p.width > sectionWidth * 0.8f)
        {
            p.width = sectionWidth * 0.8f; // Resize if too wide
            halfWidth = p.width / 2.0f;
        }

        // Place platform randomly within its section
        float minX = sectionLeft + halfWidth;
        float maxX = sectionRight - halfWidth;

        if (maxX <= minX)
        {
            newX = (sectionLeft + sectionRight) / 2.0f;
        }
        else
        {
            newX = minX + (rand() % (int)(maxX - minX));
        }
    }
    else
    {
        // Original logic for the first platform in row (maintains jump reach)
        float minReach = lastSpawnX - MAX_JUMP_REACH;
        float maxReach = lastSpawnX + MAX_JUMP_REACH;

        minReach = minReach + halfWidth;
        maxReach = maxReach - halfWidth;

        if (minReach < 50) minReach = 50;
        if (maxReach > 750) maxReach = 750;

        float range = maxReach - minReach;
        if (range <= 10.0f)
        {
            range = 10.0f;
            minReach = lastSpawnX - 5.0f;
            maxReach = lastSpawnX + 5.0f;
        }

        newX = minReach + (rand() % (int)range);
        lastSpawnX = newX;
    }

    // Final boundary checks
    if (newX - halfWidth < 20) newX = 20 + halfWidth;
    if (newX + halfWidth > 780) newX = 780 - halfWidth;

    p.x = newX;

    // NEW: Chance to spawn a power-up on this platform
    if (rand() % 100 < (POWERUP_SPAWN_CHANCE * 100))
    {
        spawnPowerUp(newX, y + 40.0f);
    }
}

void init()
{
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 800, 0, 600);
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    srand(time(0));

    loadHighscore();

    // NEW: Initialize transition variables
    transitionTimer = 0.0f;
    transitionStarted = false;
    menuSoulCurrentY = menuSoulStartY;

    // OPTIMIZATION #2: Generate Display List
    CompilePlatformGeometry();

    // OPTIMIZATION #3: Initialize Object Pool
    platforms.clear();
    powerUps.clear(); // NEW: Clear power-ups
    fireLasers.clear(); // NEW: Clear lasers

    // Initialize sound system and start menu music
    SoundSystem::getInstance()->playMenuMusic();

    // Initialize player power-up states
    soul.hasDoubleJump = false;
    soul.doubleJumpUsed = false;
    soul.speedBoostTimer = 0;
    soul.shieldTimer = 0;
    soul.shieldAlpha = 0;
    soul.speedTrailTimer = 0;
    soul.hasMagnet = false;
    soul.magnetTimer = 0;
    soul.isStunned = false; // NEW: Initialize stun state
    soul.stunTimer = 0;

    // NEW: Initialize laser attack states
    fireLasers.clear();
    laserWarningPositions.clear();
    isWarningPhase = false;
    warningPhaseTimer = 0.0f;

    // Initialize boss attack cooldown
    bossAttackCooldown = BOSS_ATTACK_COOLDOWN_MAX * 0.5f; // Start with shorter cooldown

    // Initialize player invisibility states
    soul.invisibilityTimer = 0;
    soul.isInvisible = false;
    soul.invisibilityAlpha = 1.0f;
}

void resetGame()
{
    soul.x = 400; soul.y = 200; soul.velocityY = 0;
    soul.coyoteTimer = 0; soul.isGrounded = false;

    // NEW: Reset transition variables
    transitionTimer = 0.0f;
    transitionStarted = false;
    menuSoulCurrentY = menuSoulStartY;

    // NEW: Reset power-up states
    soul.hasDoubleJump = false;
    soul.doubleJumpUsed = false;
    soul.speedBoostTimer = 0;
    soul.shieldTimer = 0;
    soul.shieldAlpha = 0;
    soul.speedTrailTimer = 0;
    soul.hasMagnet = false;
    soul.magnetTimer = 0;
    soul.isStunned = false; // NEW: Reset stun
    soul.stunTimer = 0;

    // NEW: Initialize transition variables
    transitionTimer = 0.0f;
    transitionStarted = false;
    menuSoulCurrentY = menuSoulStartY;

    // NEW: Reset laser attack states
    fireLasers.clear();
    laserWarningPositions.clear();
    isWarningPhase = false;
    warningPhaseTimer = 0.0f;

    particles.clear();
    powerUps.clear(); // NEW: Clear all power-ups
    fireLasers.clear(); // NEW: Clear all lasers

    bossY = -300.0f; bossX = 400.0f;
    totalScore = 0;
    lastSpawnX = 400.0f;
    landSquash = 0.0f;

    // NEW: Reset global effects
    globalSlowFactor = 1.0f;
    slowTimeEffectTimer = 0.0f;
    magnetRange = 0.0f;

    // NEW: Reset boss attack timers
    bossAttackCooldown = BOSS_ATTACK_COOLDOWN_MAX * 0.5f;
    bossAttackWarningTimer = 0.0f;

    // NEW: Reset invisibility states
    soul.invisibilityTimer = 0;
    soul.isInvisible = false;
    soul.invisibilityAlpha = 1.0f;
}

// NEW: Function to initialize platforms when game starts
void initializeGamePlatforms()
{
    // Clear existing platforms
    platforms.clear();
    lastSpawnX = 400.0f;

    // Create the floor
    Platform floor;
    floor.x = 400; floor.y = 50; floor.width = 800;
    floor.isBreakable = false; floor.active = true;
    platforms.push_back(floor);

    // Initial platforms - spawn with some multiple platforms per row
    for (int row = 0; row < 20; row++)
    {
        float yPos = 50 + (row * 100);

        // Determine how many platforms in this row (1-3)
        int platformsInRow = getPlatformsPerRow();

        for (int i = 0; i < platformsInRow; i++)
        {
            Platform p;
            recyclePlatform(p, yPos, platformsInRow, i, (i > 0));
            platforms.push_back(p);
        }
    }
}

// OPTIMIZATION #1: Particle System Logic
void updateParticles()
{
    Particle p;
    p.x = soul.x + (rand() % 14 - 7);
    p.y = soul.y + 20;
    p.alpha = 1.0f;
    p.size = 6.0f;
    particles.push_back(p);

    for (int i = 0; i < particles.size(); i++)
    {
        particles[i].y += 3.5f;      // Rise UPWARDS rapidly
        // Add slight horizontal drift for realism
        particles[i].x += (rand() % 3 - 1) * 0.5f;
        particles[i].alpha -= 0.03f; // Fade out moderately
        particles[i].size -= 0.15f;  // Shrink slowly
    }

    // Cleanup
    if (!particles.empty() && particles[0].alpha <= 0)
    {
        particles.erase(particles.begin());
    }
}

void updatePhysics()
{
    if (currentState != PLAYING) return;

    float currentMoveSpeed = MOVE_SPEED;
    float currentJumpForce = JUMP_FORCE;

    // NEW: Reduce movement speed if stunned
    if (soul.isStunned)
    {
        currentMoveSpeed *= 0.3f;
        currentJumpForce *= 0.3f;
    }

    if (keyStates['a'] || keyStates['A']) soul.x -= currentMoveSpeed;
    if (keyStates['d'] || keyStates['D']) soul.x += currentMoveSpeed;

    // NEW: Cap maximum horizontal velocity during speed boost
    float maxHorizontalSpeed = 20.0f; // Base max speed
    if (soul.speedBoostTimer > 0)
    {
        maxHorizontalSpeed = 40.0f; // Higher max speed during boost
    }

    // Apply velocity cap (optional damping)
    if (abs(soul.velocityX) > maxHorizontalSpeed)
    {
        soul.velocityX *= 0.95f; // Gentle damping
    }

    soul.y += soul.velocityY;

    if (soul.velocityY < 0)
    {
        soul.velocityY -= GRAVITY * FALL_MULTIPLIER;
        if (soul.coyoteTimer > 0) soul.coyoteTimer -= 1.0f;
    }
    else
    {
        soul.velocityY -= GRAVITY;
        soul.coyoteTimer = 0;
    }

    // Check for falling through bottom of screen
    if (soul.y < 0) // Player has fallen below bottom of screen
    {
        // Move camera DOWN to follow player
        float diff = soul.y; // Negative value since soul.y < 0
        soul.y = 0; // Reset to bottom of screen
        totalScore += diff; // This will DECREASE score since diff is negative

        // Move everything UP (opposite of normal scroll) to follow player down
        for (int i = 0; i < platforms.size(); i++) platforms[i].y -= diff;
        bossY -= diff;

        // Shift particles
        for (int i = 0; i < particles.size(); i++) particles[i].y -= diff;

        // Scroll power-ups
        for (auto& p : powerUps) p.y -= diff;

        // NEW: Scroll lasers
        for (auto& laser : fireLasers) laser.y -= diff;

        // Also move player slightly up to avoid being exactly at 0
        soul.y = 10.0f;
    }

    // Collision
    bool currentlyOnGround = false;
    for (int i = 0; i < platforms.size(); i++)
    {
        Platform& p = platforms[i];
        if (!p.active) continue;

        float pTop = p.y + 9.0f;
        float pLeft = p.x - (p.width / 2);
        float pRight = p.x + (p.width / 2);

        if (soul.velocityY <= 0 &&
            soul.y <= pTop && soul.y >= pTop - 20 &&
            soul.x >= pLeft && soul.x <= pRight)
        {
            if (soul.velocityY < -10.0f) landSquash = 0.4f;
            soul.y = pTop;
            soul.velocityY = 0;
            soul.isGrounded = true;
            soul.isJumping = false;
            currentlyOnGround = true;
            soul.coyoteTimer = COYOTE_LIMIT;
            if (p.isBreakable) {
                p.steppedOn = true;
                p.breakTimer -= 1.0f;
                if (p.breakTimer <= 0) p.active = false;
            }
        }
    }
    if (!currentlyOnGround) soul.isGrounded = false;

    // NEW: Only allow jumping if not stunned
    if (!soul.isStunned)
    {
        if ((keyStates['w'] || keyStates['W']) && soul.coyoteTimer > 0)
        {
            soul.velocityY = currentJumpForce;
            soul.isJumping = true;
            soul.isGrounded = false;
            soul.coyoteTimer = 0;
            landSquash = -0.2f;

            // PLAY JUMP SOUND IMMEDIATELY
            SoundSystem::getInstance()->playJumpSound();
        }

        if ((keyStates['w'] || keyStates['W']))
        {
            if (soul.coyoteTimer > 0)
            {
                soul.velocityY = currentJumpForce;
                soul.isJumping = true;
                soul.isGrounded = false;
                soul.coyoteTimer = 0;
                landSquash = -0.2f;

                // PLAY JUMP SOUND IMMEDIATELY
                SoundSystem::getInstance()->playJumpSound();
            }
            else if (soul.hasDoubleJump && !soul.doubleJumpUsed && soul.velocityY < 0)
            {
                // Double jump mid-air
                soul.velocityY = currentJumpForce * 0.8f;
                soul.doubleJumpUsed = true;
                soul.hasDoubleJump = false; // One-time use
                landSquash = -0.15f;

                // PLAY DOUBLE JUMP SOUND IMMEDIATELY
                SoundSystem::getInstance()->playDoubleJumpSound();

                // Create double jump effect particles
                for (int i = 0; i < 15; i++)
                {
                    Particle p;
                    p.x = soul.x + (rand() % 20 - 10);
                    p.y = soul.y - 10;
                    p.alpha = 1.0f;
                    p.size = 4.0f + (rand() % 8);
                    p.velocityX = (rand() % 100 - 50) * 0.05f;
                    p.velocityY = (rand() % 100) * 0.1f + 2.0f;
                    p.color[0] = 0.2f; p.color[1] = 1.0f; p.color[2] = 0.3f;
                    particles.push_back(p);
                }
            }
        }
    }

    // Camera Scroll
    if (soul.y > SCROLL_THRESHOLD)
    {
        float diff = soul.y - SCROLL_THRESHOLD;
        soul.y = SCROLL_THRESHOLD;
        totalScore += diff;
        for (int i = 0; i < platforms.size(); i++) platforms[i].y -= diff;
        bossY -= diff;
        // Shift particles down too so they stay in world space
        for (int i = 0; i < particles.size(); i++) particles[i].y -= diff;

        // Scroll power-ups (they will be removed in updatePowerUps() if too far down)
        for (auto& p : powerUps)
        {
            p.y -= diff;
        }

        // NEW: DO NOT scroll warning symbols - they stay fixed at bottom
        // Scroll only ACTUAL lasers
        for (auto& laser : fireLasers)
        {
            if (!laser.isWarning) // Only move actual lasers
            {
                laser.y -= diff;
            }
            // Warning symbols stay at fixed bottom position
        }
    }

    // Boss Logic
    bossX += (soul.x - bossX) * 0.05f * globalSlowFactor;
    float targetY = -40.0f;
    if (totalScore < 3000.0f) targetY = -200.0f;
    float distFromTarget = targetY - bossY;
    float currentSpeed = 1.0f;
    if (distFromTarget > 0) currentSpeed += (distFromTarget / 60.0f);
    else currentSpeed *= 0.5f;
    currentSpeed += (totalScore / 15000.0f);
    bossY += currentSpeed * globalSlowFactor;

    // NEW: Check collision with boss (with shield protection)
    bool hitByBoss = false;
    // In updatePhysics() function, modify the boss collision check:
    if (bossY > soul.y - 30 || soul.y < 0)
    {
        // NEW: Check if player is invisible
        if (soul.isInvisible)
        {
            // Invisible player cannot be hit by boss
            // Optional: Create a "dodge" effect
            if (rand() % 10 == 0)
            {
                Particle p;
                p.x = soul.x;
                p.y = soul.y;
                p.alpha = 0.7f;
                p.size = 3.0f + (rand() % 6);
                p.velocityX = (rand() % 200 - 100) * 0.05f;
                p.velocityY = (rand() % 200 - 100) * 0.05f;
                p.color[0] = 0.9f; p.color[1] = 0.9f; p.color[2] = 1.0f;
                particles.push_back(p);
            }
        }
        else if (soul.shieldTimer > 0) // Existing shield logic
        {
            // Shield protects from one hit
            soul.shieldTimer = 0;
            soul.shieldAlpha = 0;

            // Create shield break effect
            for (int i = 0; i < 30; i++)
            {
                Particle p;
                p.x = soul.x;
                p.y = soul.y;
                p.alpha = 1.0f;
                p.size = 3.0f + (rand() % 10);
                p.velocityX = (rand() % 200 - 100) * 0.1f;
                p.velocityY = (rand() % 200 - 100) * 0.1f;
                p.color[0] = 0.2f; p.color[1] = 0.6f; p.color[2] = 1.0f;
                particles.push_back(p);
            }
        }
        else // No shield or invisibility
        {
            hitByBoss = true;
        }
    }

    if (hitByBoss)
    {
        SoundSystem::getInstance()->stopAllSounds();
        SoundSystem::getInstance()->playGameOverSound();
        float currentMeters = totalScore / 50.0f;
        updateAndSaveHighscore(currentMeters);
        currentState = GAMEOVER;
    }

    // MODIFIED: Platforms DON'T get deleted when they go off screen
    // We only deactivate breakable platforms that have broken
    for (int i = 0; i < platforms.size(); i++)
    {
        if (platforms[i].isBreakable && platforms[i].breakTimer <= 0)
        {
            platforms[i].active = false;

        }
    }

    // NEW: Always ensure we have enough platforms above and below
    // Find highest and lowest platform Y positions
    float highestY = -100000.0f;
    float lowestY = 100000.0f;

    for (int i = 0; i < platforms.size(); i++)
    {
        if (platforms[i].active)
        {
            if (platforms[i].y > highestY) highestY = platforms[i].y;
            if (platforms[i].y < lowestY) lowestY = platforms[i].y;
        }
    }

    // Spawn platforms above if needed
    while (highestY < SCROLL_THRESHOLD + 200.0f)
    {
        // NEW: Determine how many platforms to spawn in this row (1-3)
        int platformsInRow = getPlatformsPerRow();

        // Spawn platforms for this row
        for (int i = 0; i < platformsInRow; i++)
        {
            // Find an inactive platform to recycle
            int platformToRecycle = -1;
            for (int j = 0; j < platforms.size(); j++)
            {
                if (!platforms[j].active)
                {
                    platformToRecycle = j;
                    break;
                }
            }

            // If no inactive platform found, create a new one
            if (platformToRecycle == -1)
            {
                Platform newPlatform;
                platforms.push_back(newPlatform);
                platformToRecycle = platforms.size() - 1;
            }

            // Calculate Y position for this row
            float newY = highestY + 120.0f;

            // Recycle the platform
            if (i == 0)
            {
                // First platform in row uses jump reach logic
                recyclePlatform(platforms[platformToRecycle], newY, platformsInRow, i, false);
            }
            else
            {
                // Additional platforms use random positioning
                recyclePlatform(platforms[platformToRecycle], newY, platformsInRow, i, true);
            }
        }

        highestY += 120.0f;
    }

    // Spawn platforms below if needed (when player falls)
    while (lowestY > soul.y - 400.0f) // Keep platforms 400px below player
    {
        // NEW: Determine how many platforms to spawn in this row (1-3)
        int platformsInRow = getPlatformsPerRow();

        // Spawn platforms for this row
        for (int i = 0; i < platformsInRow; i++)
        {
            // Find an inactive platform to recycle
            int platformToRecycle = -1;
            for (int j = 0; j < platforms.size(); j++)
            {
                if (!platforms[j].active)
                {
                    platformToRecycle = j;
                    break;
                }
            }

            // If no inactive platform found, create a new one
            if (platformToRecycle == -1)
            {
                Platform newPlatform;
                platforms.push_back(newPlatform);
                platformToRecycle = platforms.size() - 1;
            }

            // Calculate Y position for this row (BELOW current lowest)
            float newY = lowestY - 120.0f;

            // Recycle the platform
            if (i == 0)
            {
                // First platform in row uses jump reach logic
                recyclePlatform(platforms[platformToRecycle], newY, platformsInRow, i, false);
            }
            else
            {
                // Additional platforms use random positioning
                recyclePlatform(platforms[platformToRecycle], newY, platformsInRow, i, true);
            }
        }

        lowestY -= 120.0f;
    }


    // NEW: Update power-up systems
    updatePowerUps();
    updatePlayerPowerUps();

    // NEW: Update boss attack systems
    updateBossAttack();
    updateFireLasers();
    updatePlayerStun();

    updateParticles();

    // NEW: Screen Wrapping - when player goes off left/right edges
    if (soul.x < -SCREEN_WRAP_OFFSET)
    {
        // Calculate how far off-screen we are
        float offScreenAmount = -SCREEN_WRAP_OFFSET - soul.x;
        soul.x = 800 + SCREEN_WRAP_OFFSET - offScreenAmount;

        // Create visual effect for wrapping
        for (int i = 0; i < 10; i++)
        {
            Particle p;
            p.x = 0; // Left edge
            p.y = soul.y + (rand() % 60 - 30);
            p.alpha = 0.8f;
            p.size = 5.0f + (rand() % 10);
            p.velocityX = 3.0f + (rand() % 10);
            p.velocityY = (rand() % 100 - 50) * 0.1f;
            p.color[0] = 0.2f; p.color[1] = 0.9f; p.color[2] = 1.0f;
            particles.push_back(p);
        }
    }
    else if (soul.x > 800 + SCREEN_WRAP_OFFSET)
    {
        // Calculate how far off-screen we are
        float offScreenAmount = soul.x - (800 + SCREEN_WRAP_OFFSET);
        soul.x = -SCREEN_WRAP_OFFSET + offScreenAmount;

        // Create visual effect for wrapping
        for (int i = 0; i < 10; i++)
        {
            Particle p;
            p.x = 800; // Right edge
            p.y = soul.y + (rand() % 60 - 30);
            p.alpha = 0.8f;
            p.size = 5.0f + (rand() % 10);
            p.velocityX = -3.0f - (rand() % 10);
            p.velocityY = (rand() % 100 - 50) * 0.1f;
            p.color[0] = 0.2f; p.color[1] = 0.9f; p.color[2] = 1.0f;
            particles.push_back(p);
        }
    }
    // Call the safe wrapping handler
    handleScreenWrapping();

    // Original boundary check (keep this for vertical boundaries)
    if (soul.y < 0)
    {
        // Player fell to bottom (game over condition)
        if (soul.shieldTimer > 0)
        {
            // Shield protects from one hit
            soul.shieldTimer = 0;
            soul.shieldAlpha = 0;
            soul.y = 50; // Reset to safe position

            // Create shield break effect
            for (int i = 0; i < 30; i++)
            {
                Particle p;
                p.x = soul.x;
                p.y = soul.y;
                p.alpha = 1.0f;
                p.size = 3.0f + (rand() % 10);
                p.velocityX = (rand() % 200 - 100) * 0.1f;
                p.velocityY = (rand() % 200 - 100) * 0.1f;
                p.color[0] = 0.2f; p.color[1] = 0.6f; p.color[2] = 1.0f;
                particles.push_back(p);
            }
        }
        else
        {
            // Game over
            float currentMeters = totalScore / 50.0f;
            updateAndSaveHighscore(currentMeters);
            currentState = GAMEOVER;
        }
    }

    // Soft boundaries - gently push back if near edges (but allow wrapping)
    if (soul.x < -50 && soul.x > -SCREEN_WRAP_OFFSET)
    {
        soul.velocityX += 0.3f; // Gentle push toward center
    }
    else if (soul.x > 850 && soul.x < 800 + SCREEN_WRAP_OFFSET)
    {
        soul.velocityX -= 0.3f; // Gentle push toward center
    }
}

// ==========================================
// 6. DISPLAY & MAIN
// ==========================================

void timer(int)
{
    g_Time += 0.02f;
    hellGlowPulse += 0.02f;  // Slow breathing pulse for the entire background

    // Update pause menu fade effect
    if (currentState == PAUSED && pauseMenuAlpha < 1.0f)
    {
        pauseMenuAlpha += PAUSE_FADE_SPEED * 0.016f; // Assuming 60fps
        if (pauseMenuAlpha > 1.0f) pauseMenuAlpha = 1.0f;
    }

    // Update transition if in transition state
    if (currentState == TRANSITION)
    {
        updateTransition();
    }
    else if (currentState == PLAYING)
    {
        updatePhysics();
        landSquash *= 0.85f;
    }

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void display()
{
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    // === PASTE THE ENTIRE BACKGROUND CODE RIGHT HERE ===
    // === FULL HELLISH LAVA CAVE BACKGROUND (Menu & Game) ===
    glDisable(GL_BLEND);

    // 1. Deep dark base
    glClearColor(0.05f, 0.02f, 0.01f, 1.0f);

    // 2. Soft vertical gradient
    for (int i = 0; i < 100; i++) {
        float y = i * 6.0f;
        float t = y / 600.0f;
        float glow = 0.15f + sin(hellGlowPulse + t * 3.0f) * 0.05f;
        glColor3f(0.3f * t + glow, 0.08f * t + glow * 0.5f, 0.02f * t);
        glBegin(GL_QUAD_STRIP);
        glVertex2f(0, y);
        glVertex2f(800, y);
        glVertex2f(0, y + 6.0f);
        glVertex2f(800, y + 6.0f);
        glEnd();
    }

    // 3. Top jagged black spikes — pure dark silhouette (classic hell look)
    glColor4f(0.05f, 0.01f, 0.01f, 1.0f);  // Almost pure black for strong silhouette

    float peakWave = sin(hellGlowPulse * 0.6f) * 8.0f;  // Subtle wave animation

    for (int i = 0; i < 28; i++) {  // More spikes for dense coverage
        float segmentWidth = 28.0f;
        float x = i * segmentWidth + segmentWidth * 0.5f + sin(i * 1.3f + hellGlowPulse) * 8.0f;

        float baseY = 600.0f;  // Anchored at top edge
        float peakHeight = 90.0f + sin(i * 1.1f + hellGlowPulse) * 35.0f + peakWave;
        float peakY = baseY - peakHeight;

        float baseWidth = segmentWidth * 0.9f;  // Wide base
        float midWidth = baseWidth * 0.3f;     // Tapers to point

        // Main dark spike
        glBegin(GL_TRIANGLES);
        glVertex2f(x - baseWidth * 0.5f, baseY);         // Left base
        glVertex2f(x + baseWidth * 0.5f, baseY);         // Right base
        glVertex2f(x, peakY);                            // Sharp tip
        glEnd();

        // Optional very subtle dark inner shadow (keeps it black, no orange)
        glColor4f(0.02f, 0.01f, 0.01f, 1.0f);
        glBegin(GL_TRIANGLES);
        glVertex2f(x - midWidth * 0.5f, baseY - 5.0f);
        glVertex2f(x + midWidth * 0.5f, baseY - 5.0f);
        glVertex2f(x, peakY + 15.0f);
        glEnd();
    }

    // 4. Bottom lava with glowing cracks
    float lavaBase = 50.0f + sin(hellGlowPulse * 0.5f) * 15.0f;

    glColor3f(0.8f, 0.2f, 0.05f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(800, 0);
    glVertex2f(800, lavaBase + 50.0f);
    glVertex2f(0, lavaBase + 50.0f);
    glEnd();

    glColor3f(1.0f, 0.4f, 0.1f);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= 100; i++) {
        float x = i * 8.0f;
        float crustWave = sin(x * 0.02f + hellGlowPulse * 1.5f) * 30.0f + sin(x * 0.05f) * 15.0f;
        float yTop = lavaBase + crustWave;
        float yBottom = lavaBase + crustWave * 0.6f;

        glVertex2f(x, yBottom);
        glVertex2f(x, yTop);
    }
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (int i = 0; i < 40; i++) {
        float x = 20.0f + i * 20.0f + sin(i + hellGlowPulse) * 10.0f;
        float crackY = lavaBase + sin(x * 0.03f + hellGlowPulse) * 25.0f;
        float length = 20.0f + sin(i * 1.1f) * 15.0f;

        glColor4f(1.0f, 0.7f, 0.2f, 0.8f);
        glBegin(GL_QUADS);
        glVertex2f(x - 4, crackY - length);
        glVertex2f(x + 4, crackY - length);
        glVertex2f(x + 2, crackY);
        glVertex2f(x - 2, crackY);
        glEnd();
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    // === END BACKGROUND ===

    if (currentState == MENU)
    {
        drawStartMenu();
    }
    else if (currentState == TRANSITION)
    {
        // Draw transition scene
        drawStartMenu(); // This will draw only the falling soul and platforms
    }
    else if (currentState == PAUSED)  // ADD THIS CONDITION
    {
        // Draw the game in background (but frozen)
        DrawBoss(bossX, bossY, 100.0f);

        // Draw fire lasers
        for (const auto& laser : fireLasers)
        {
            if (laser.active)
            {
                float laserHeight = 600;
                DrawFireLaser(laser.x, laser.y, laser.width, laserHeight,
                    laser.chargeTimer, laser.isWarning, laser.warningProgress);
                if (laser.isWarning)
                {
                    DrawWarningSymbol(laser.x, 50.0f, laser.warningProgress, 30.0f);
                }
            }
        }

        // Draw Platforms
        for (int i = 0; i < platforms.size(); i++)
        {
            if (!platforms[i].active) continue;

            if (platforms[i].isBreakable)
            {
                float shake = 0;
                if (platforms[i].steppedOn && platforms[i].breakTimer < 20) shake = (rand() % 5) - 2;
                DrawPlatformBreakable(platforms[i].x + shake, platforms[i].y, platforms[i].width, 50.0f);
            }
            else
            {
                glPushMatrix();
                glTranslatef(platforms[i].x, platforms[i].y, 0.0f);
                glScalef(platforms[i].width, 50.0f, 1.0f);
                glCallList(platformDisplayList);
                glPopMatrix();
            }
        }

        // Draw Power-ups
        for (const auto& p : powerUps)
        {
            if (p.active)
            {
                float glow = 0.7f + sin(g_Time * 5.0f) * 0.3f;
                DrawPowerUp(p.x, p.y, p.type, p.rotation, glow);
            }
        }

        // Draw Particles (frozen in place)
        glEnable(GL_BLEND);
        for (int i = 0; i < particles.size(); i++)
        {
            if (particles[i].color[0] >= 0)
            {
                glColor4f(particles[i].color[0], particles[i].color[1],
                    particles[i].color[2], particles[i].alpha);
            }
            else
            {
                glColor4f(0.1f, 0.5f, 1.0f, particles[i].alpha);
            }

            glPushMatrix();
            glTranslatef(particles[i].x, particles[i].y, 0.0f);

            float s = (particles[i].size > 0.0f) ? particles[i].size : 0.0f;

            glRotatef(45.0f, 0.0f, 0.0f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(-s, -s); glVertex2f(s, -s);
            glVertex2f(s, s); glVertex2f(-s, s);
            glEnd();
            glPopMatrix();
        }

        // Draw Player
        DrawSpirit(soul.x, soul.y + 28, 50.0f, 1.0f, 1.0f);

        // Draw UI
        float currentMeters = totalScore / 50.0f;
        char buffer[100];
        sprintf_s(buffer, sizeof(buffer), "Distance: %.1f m", currentMeters);
        glColor3f(0.0f, 0.0f, 0.0f); renderBitmapString(22.0f, 568.0f, GLUT_BITMAP_HELVETICA_18, buffer);
        glColor3f(0.2f, 0.9f, 1.0f); renderBitmapString(20.0f, 570.0f, GLUT_BITMAP_HELVETICA_18, buffer);

        // Now draw the pause menu overlay
        drawPauseMenu();
    }
    else if (currentState == GAMEOVER)
    {
        drawGameOverMenu();
    }
    else
    {
        DrawBoss(bossX, bossY, 100.0f);

        // NEW: Draw fire lasers
        for (const auto& laser : fireLasers)
        {
            if (laser.active)
            {
                float laserHeight = 600;
                DrawFireLaser(laser.x, laser.y, laser.width, laserHeight,
                    laser.chargeTimer, laser.isWarning, laser.warningProgress);
                if (laser.isWarning)
                {
                    DrawWarningSymbol(laser.x, 50.0f, laser.warningProgress, 30.0f);
                }
            }
        }

        if (isWarningPhase)
        {
            // Additional warning based on player position
            bool playerNearWarning = false;
            for (const auto& laser : fireLasers)
            {
                if (laser.active && laser.isWarning)
                {
                    float dx = laser.x - soul.x;
                    if (abs(dx) < 120.0f) // If player is horizontally close to warning
                    {
                        playerNearWarning = true;
                        break;
                    }
                }
            }
        }

        // Draw Platforms
        for (int i = 0; i < platforms.size(); i++)
        {
            if (!platforms[i].active) continue;

            if (platforms[i].isBreakable)
            {
                float shake = 0;
                if (platforms[i].steppedOn && platforms[i].breakTimer < 20) shake = (rand() % 5) - 2;
                DrawPlatformBreakable(platforms[i].x + shake, platforms[i].y, platforms[i].width, 50.0f);
            }
            else
            {
                // OPTIMIZATION #2: USE DISPLAY LIST
                glPushMatrix();
                glTranslatef(platforms[i].x, platforms[i].y, 0.0f);
                glScalef(platforms[i].width, 50.0f, 1.0f);
                glCallList(platformDisplayList); // INSTANT DRAW
                glPopMatrix();
            }
        }

        // NEW: Draw Power-ups
        for (const auto& p : powerUps)
        {
            if (p.active)
            {
                float glow = 0.7f + sin(g_Time * 5.0f) * 0.3f;
                DrawPowerUp(p.x, p.y, p.type, p.rotation, glow);
            }
        }

        //Draw Particles
        glEnable(GL_BLEND);
        for (int i = 0; i < particles.size(); i++)
        {
            // Use particle's color if it has one
            if (particles[i].color[0] >= 0) // Check if color was set
            {
                glColor4f(particles[i].color[0], particles[i].color[1],
                    particles[i].color[2], particles[i].alpha);
            }
            else
            {
                glColor4f(0.1f, 0.5f, 1.0f, particles[i].alpha);
            }

            glPushMatrix();
            glTranslatef(particles[i].x, particles[i].y, 0.0f);

            float s = (particles[i].size > 0.0f) ? particles[i].size : 0.0f;

            glRotatef(45.0f, 0.0f, 0.0f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(-s, -s); glVertex2f(s, -s);
            glVertex2f(s, s); glVertex2f(-s, s);
            glEnd();
            glPopMatrix();
        }

        // NEW: Draw player power-up effects
        if (soul.shieldAlpha > 0)
        {
            DrawShieldEffect(soul.x, soul.y + 28, soul.shieldAlpha);
        }

        if (soul.hasMagnet)
        {
            DrawMagnetField(soul.x, soul.y + 28, magnetRange);
        }
        if (soul.speedBoostTimer > 0)
        {
            DrawFlyEffect(soul.x, soul.y + 28, 0.8f);
        }

        // NEW: Draw invisibility effect
        if (soul.isInvisible)
        {
            DrawInvisibilityEffect(soul.x, soul.y + 28, soul.invisibilityAlpha);
        }

        // NEW: Draw stun effect if player is stunned
        if (soul.isStunned)
        {
            float stunAlpha = soul.stunTimer / STUN_DURATION;
            DrawStunEffect(soul.x, soul.y + 28, stunAlpha);
        }

        // Draw Player
        float stretchX = 1.0f, stretchY = 1.0f;
        float speedStretch = abs(soul.velocityY) / 40.0f;
        if (speedStretch > 0.3f) speedStretch = 0.3f;
        stretchY += speedStretch; stretchX -= speedStretch;
        stretchX += landSquash; stretchY -= landSquash;
        // NEW: Add visual feedback for double jump available
        if (soul.hasDoubleJump && !soul.doubleJumpUsed)
        {
            // Slight pulsing effect when double jump is available
            float pulse = sin(g_Time * 8.0f) * 0.05f;
            stretchX += pulse;
            stretchY += pulse;
        }

        // NEW: Add trembling effect when stunned
        if (soul.isStunned)
        {
            float tremble = sin(g_Time * 20.0f) * 0.05f;
            stretchX += tremble;
            stretchY += tremble;
        }

        DrawSpirit(soul.x, soul.y + 28, 50.0f, stretchX, stretchY);

        // NEW: Visual effect for screen wrapping
        if (soul.x < 50 || soul.x > 750)
        {
            // Draw screen edge warning effect
            float alpha = fabs(sin(g_Time * 10.0f)) * 0.3f;
            glColor4f(1.0f, 0.5f, 0.2f, alpha);

            if (soul.x < 50)
            {
                // Left edge warning
                glBegin(GL_QUADS);
                glVertex2f(0, 0);
                glVertex2f(30, 0);
                glVertex2f(30, 600);
                glVertex2f(0, 600);
                glEnd();
            }
            else if (soul.x > 750)
            {
                // Right edge warning
                glBegin(GL_QUADS);
                glVertex2f(770, 0);
                glVertex2f(800, 0);
                glVertex2f(800, 600);
                glVertex2f(770, 600);
                glEnd();
            }
        }

        // UI
        float currentMeters = totalScore / 50.0f;
        char buffer[100];
        sprintf_s(buffer, sizeof(buffer), "Distance: %.1f m", currentMeters);
        glColor3f(0.0f, 0.0f, 0.0f); renderBitmapString(22.0f, 568.0f, GLUT_BITMAP_HELVETICA_18, buffer);
        glColor3f(0.2f, 0.9f, 1.0f); renderBitmapString(20.0f, 570.0f, GLUT_BITMAP_HELVETICA_18, buffer);

        sprintf_s(buffer, sizeof(buffer), "Best: %.1f m", bestDistance);
        glColor3f(0.0f, 0.0f, 0.0f); renderBitmapString(22.0f, 543.0f, GLUT_BITMAP_HELVETICA_18, buffer);
        glColor3f(1.0f, 0.8f, 0.0f); renderBitmapString(20.0f, 545.0f, GLUT_BITMAP_HELVETICA_18, buffer);

        // NEW: Power-up status display
        int yOffset = 520;

        // Speed Boost
        if (soul.speedBoostTimer > 0)
        {
            sprintf_s(buffer, sizeof(buffer), "Speed Boost: %.1fs", soul.speedBoostTimer);
            glColor3f(1.0f, 0.8f, 0.2f);
            renderBitmapString(20.0f, yOffset, GLUT_BITMAP_HELVETICA_12, buffer);
            yOffset -= 20;
        }

        // NEW: Invisibility
        if (soul.isInvisible)
        {
            sprintf_s(buffer, sizeof(buffer), "Invisible: %.1fs", soul.invisibilityTimer);
            glColor3f(0.9f, 0.9f, 1.0f);
            renderBitmapString(20.0f, yOffset, GLUT_BITMAP_HELVETICA_12, buffer);
            yOffset -= 20;
        }

        // Double Jump
        if (soul.hasDoubleJump && !soul.doubleJumpUsed)
        {
            glColor3f(0.2f, 1.0f, 0.3f);
            renderBitmapString(20.0f, yOffset, GLUT_BITMAP_HELVETICA_12, "Double Jump Ready!");
            yOffset -= 20;
        }

        // Slow Time
        if (slowTimeEffectTimer > 0)
        {
            sprintf_s(buffer, sizeof(buffer), "Slow Time: %.1fs", slowTimeEffectTimer);
            glColor3f(0.8f, 0.2f, 1.0f);
            renderBitmapString(20.0f, yOffset, GLUT_BITMAP_HELVETICA_12, buffer);
            yOffset -= 20;
        }

        // Magnet
        if (soul.hasMagnet)
        {
            sprintf_s(buffer, sizeof(buffer), "Magnet: %.1fs", soul.magnetTimer);
            glColor3f(1.0f, 0.3f, 0.3f);
            renderBitmapString(20.0f, yOffset, GLUT_BITMAP_HELVETICA_12, buffer);
            yOffset -= 20;
        }
    }
    glutSwapBuffers();
}

void keyDown(unsigned char key, int x, int y)
{
    keyStates[key] = true;

    // Handle ESC key for pause (only trigger once per press)
    if (key == 27 && !isPausedKeyPressed) // ESC key
    {
        isPausedKeyPressed = true;

        if (currentState == PLAYING)
        {
            currentState = PAUSED;
            pauseMenuAlpha = 0.0f; // Start fade-in
            SoundSystem::getInstance()->stopAllSounds();
            SoundSystem::getInstance()->playPauseSound();
        }
        else if (currentState == PAUSED)
        {
            currentState = PLAYING;
            pauseMenuAlpha = 0.0f;
            SoundSystem::getInstance()->playUnPauseSound();
            SoundSystem::getInstance()->playGameMusic();
        }
        else if (currentState == MENU)
        {
            // Toggle exit dialog in main menu
            showExitDialog = !showExitDialog;
            SoundSystem::getInstance()->stopAllSounds();
            SoundSystem::getInstance()->playPauseSound();
        }

    }
    else if (key == 13) // Enter key
    {
        SoundSystem::getInstance()->playSelectSound();

        if (currentState == MENU)
        {
            if (showExitDialog)
            {
                // Confirm exit
                exit(0);
            }

            // Start transition instead of immediately starting game
            currentState = TRANSITION;
            transitionStarted = true;
            transitionTimer = 0.0f;
            menuSoulCurrentY = menuSoulStartY;

            // Make sure game is initialized
            resetGame();

            // Initialize platforms for transition view
            particles.clear();
        }
        else if (currentState == PAUSED)
        {
            // Handle pause menu selection
            switch (pauseMenuSelection)
            {
            case 0: // Resume
                currentState = PLAYING;
                pauseMenuAlpha = 0.0f;
                SoundSystem::getInstance()->playGameMusic();
                break;
            case 1: // Restart
                resetGame();
                currentState = PLAYING;
                pauseMenuAlpha = 0.0f;

                // Start game music
                SoundSystem::getInstance()->playGameMusic();
                break;
            case 2: // Back to Main Menu
                currentState = MENU;
                pauseMenuAlpha = 0.0f;

                // Start game music
                SoundSystem::getInstance()->playMenuMusic();

                // Reset game but keep highscore
                resetGame();
                break;
            }
        }
    }

    // Handle Game Over state
    else if (currentState == GAMEOVER)
    {
        if (key == 'r' || key == 'R')
        {
            // Stop any current sounds
            SoundSystem::getInstance()->stopAllSounds();

            SoundSystem::getInstance()->playSelectSound();

            // Play game start sound
            SoundSystem::getInstance()->playGameStartSound();

            // Start game music
            SoundSystem::getInstance()->playGameMusic();

            resetGame();
            initializeGamePlatforms();
            currentState = PLAYING;
        }
        else if (key == 'm' || key == 'M')
        {
            SoundSystem::getInstance()->playSelectSound();
            currentState = MENU;
        }
    }
}

void keyUp(unsigned char key, int x, int y)
{
    keyStates[key] = false;

    // Reset paused key flag when ESC is released
    if (key == 27)
    {
        isPausedKeyPressed = false;
    }
}

void specialKeyDown(int key, int x, int y)
{
    if (currentState == PAUSED)
    {
        switch (key)
        {
        case GLUT_KEY_UP:
            pauseMenuSelection--;
            SoundSystem::getInstance()->playArrowSelectSound();
            if (pauseMenuSelection < 0) pauseMenuSelection = PAUSE_OPTIONS_COUNT - 1;
            break;

        case GLUT_KEY_DOWN:
            SoundSystem::getInstance()->playArrowSelectSound();
            pauseMenuSelection = (pauseMenuSelection + 1) % PAUSE_OPTIONS_COUNT;
            break;
        }
    }
}

int main(int argc, char** argv)
{
    soul.x = 400; soul.y = 200; soul.velocityY = 0;
    for (int i = 0; i < 256; i++) keyStates[i] = false;

    // Initialize player power-up states
    soul.hasDoubleJump = false;
    soul.doubleJumpUsed = false;
    soul.speedBoostTimer = 0;
    soul.shieldTimer = 0;
    soul.shieldAlpha = 0;
    soul.speedTrailTimer = 0;
    soul.hasMagnet = false;
    soul.magnetTimer = 0;
    soul.isStunned = false; // NEW: Initialize stun state
    soul.stunTimer = 0;

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(800, 600);
    glutCreateWindow("Soulscape: Infinite Climb");

    // Load highscore at program start
    loadHighscore();

    init();
    glutDisplayFunc(display);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutSpecialFunc(specialKeyDown);
    glutTimerFunc(0, timer, 0);

    glutMainLoop();

    SoundSystem::getInstance()->cleanup();

    return 0;
}
