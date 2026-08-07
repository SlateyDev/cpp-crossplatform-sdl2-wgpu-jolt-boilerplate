#include "AudioManager.hpp"

void AudioManager::ChunkDeleter::operator()(Mix_Chunk* chunk) const
{
    if (chunk) {
        Mix_FreeChunk(chunk);
    }
}

void AudioManager::MusicDeleter::operator()(Mix_Music* music) const
{
    if (music) {
        Mix_FreeMusic(music);
    }
}

AudioManager::~AudioManager()
{
    Shutdown();
}

bool AudioManager::Initialize(std::string& outError, const int mixingChannels)
{
    outError.clear();

    if (initialized) {
        return true;
    }

    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        outError = Mix_GetError();
        return false;
    }

    if (Mix_AllocateChannels(mixingChannels) < 1) {
        outError = Mix_GetError();
        Mix_CloseAudio();
        return false;
    }
    initialized = true;
    return true;
}

void AudioManager::Shutdown()
{
    if (!initialized) {
        return;
    }

    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    musicTracks.clear();
    soundEffects.clear();
    Mix_CloseAudio();
    initialized = false;
}

bool AudioManager::LoadSoundEffect(const std::string& name, const std::string& path, std::string& outError)
{
    outError.clear();
    if (!initialized) {
        outError = "AudioManager is not initialized";
        return false;
    }

    auto chunk = std::unique_ptr<Mix_Chunk, ChunkDeleter>(Mix_LoadWAV(path.c_str()));
    if (!chunk) {
        outError = Mix_GetError();
        return false;
    }

    soundEffects[name] = std::move(chunk);
    return true;
}

bool AudioManager::LoadMusic(const std::string& name, const std::string& path, std::string& outError)
{
    outError.clear();
    if (!initialized) {
        outError = "AudioManager is not initialized";
        return false;
    }

    auto music = std::unique_ptr<Mix_Music, MusicDeleter>(Mix_LoadMUS(path.c_str()));
    if (!music) {
        outError = Mix_GetError();
        return false;
    }

    musicTracks[name] = std::move(music);
    return true;
}

int AudioManager::PlaySoundEffect(const std::string& name, const int loops) const
{
    if (!initialized) {
        return -1;
    }
    const auto found = soundEffects.find(name);
    if (found == soundEffects.end()) {
        return -1;
    }
    return Mix_PlayChannel(-1, found->second.get(), loops);
}

bool AudioManager::PlayMusic(const std::string& name, const int loops) const
{
    if (!initialized) {
        return false;
    }
    const auto found = musicTracks.find(name);
    if (found == musicTracks.end()) {
        return false;
    }
    return Mix_PlayMusic(found->second.get(), loops) == 0;
}

bool AudioManager::IsMusicPlaying() const
{
    return initialized && Mix_PlayingMusic() != 0;
}

void AudioManager::StopMusic() const
{
    if (initialized) {
        Mix_HaltMusic();
    }
}

bool AudioManager::IsInitialized() const
{
    return initialized;
}
