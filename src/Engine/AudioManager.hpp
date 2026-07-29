#ifndef ABERRANT_ENGINE_AUDIOMANAGER_HPP
#define ABERRANT_ENGINE_AUDIOMANAGER_HPP

#include <SDL_mixer.h>

#include <memory>
#include <string>
#include <unordered_map>

class AudioManager
{
public:
    AudioManager() = default;
    ~AudioManager();

    bool Initialize(std::string& outError, int mixingChannels = 32);
    void Shutdown();

    bool LoadSoundEffect(const std::string& name, const std::string& path, std::string& outError);
    bool LoadMusic(const std::string& name, const std::string& path, std::string& outError);

    int PlaySoundEffect(const std::string& name, int loops = 0) const;
    bool PlayMusic(const std::string& name, int loops = -1) const;
    void StopMusic() const;
    bool IsInitialized() const;

private:
    struct ChunkDeleter {
        void operator()(Mix_Chunk* chunk) const;
    };
    struct MusicDeleter {
        void operator()(Mix_Music* music) const;
    };

    std::unordered_map<std::string, std::unique_ptr<Mix_Chunk, ChunkDeleter>> soundEffects;
    std::unordered_map<std::string, std::unique_ptr<Mix_Music, MusicDeleter>> musicTracks;
    bool initialized = false;
};

#endif
