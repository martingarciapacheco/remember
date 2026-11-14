#import <MediaPlayer/MediaPlayer.h>
#include "MacMediaTransport.h"

struct MacMediaTransport::Impl
{
    struct Commands
    {
        std::atomic<bool> alive { true };
        std::atomic<unsigned> enabled { 0 };
        std::function<void(int)> perform;
    };
    explicit Impl (std::function<State()> read, std::function<void(int)> action) : state (std::move (read))
    {
        commands->perform = std::move (action);
        @autoreleasepool
        {
            auto* centre = [MPRemoteCommandCenter sharedCommandCenter];
            MPRemoteCommand* native[] { centre.previousTrackCommand, centre.togglePlayPauseCommand,
                centre.nextTrackCommand, centre.playCommand, centre.pauseCommand, centre.stopCommand };
            for (int i = 0; i < 6; ++i)
            {
                auto context = commands;
                id token = [native[i] addTargetWithHandler: ^MPRemoteCommandHandlerStatus (MPRemoteCommandEvent*)
                {
                    if (! context->alive.load() || (context->enabled.load() & (1u << i)) == 0)
                        return MPRemoteCommandHandlerStatusNoActionableNowPlayingItem;
                    const bool queued = juce::MessageManager::callAsync ([context, i]
                    {
                        if (context->alive.load() && (context->enabled.load() & (1u << i)) != 0)
                            context->perform (i);
                    });
                    return queued ? MPRemoteCommandHandlerStatusSuccess : MPRemoteCommandHandlerStatusCommandFailed;
                }];
                targets.emplace_back (native[i], [token retain]);
                native[i].enabled = NO;
            }
        }
    }
    ~Impl()
    {
        commands->alive.store (false); commands->enabled.store (0);
        @autoreleasepool
        {
            for (const auto& target : targets)
            { [target.first removeTarget: target.second]; target.first.enabled = NO; [target.second release]; }
            if (published)
            {
                auto* centre = [MPNowPlayingInfoCenter defaultCenter];
                centre.nowPlayingInfo = nil; centre.playbackState = MPNowPlayingPlaybackStateUnknown;
            }
        }
    }
    void update()
    {
        const auto next = state();
        // Browsing or constructing the app must not take media-key ownership
        // from another player. Publish only after Remember actually plays.
        active = next.playing || (active && next.canPlay);
        constexpr unsigned trackCommands = (1u << 0) | (1u << 2);
        constexpr unsigned playbackCommands = (1u << 1) | (1u << 3) | (1u << 4) | (1u << 5);
        const unsigned enabled = active ? (next.hasQueue ? trackCommands : 0u) | (next.canPlay ? playbackCommands : 0u) : 0;
        commands->enabled.store (enabled);
        @autoreleasepool
        {
            for (size_t i = 0; i < targets.size(); ++i) targets[i].first.enabled = (enabled & (1u << i)) != 0;
            auto* centre = [MPNowPlayingInfoCenter defaultCenter];
            if (! active)
            {
                if (published) { centre.nowPlayingInfo = nil; centre.playbackState = MPNowPlayingPlaybackStateUnknown; }
                published = false; return;
            }
            const auto title = next.title.isEmpty() ? juce::String ("Remember") : next.title;
            const auto duration = std::isfinite (next.duration) ? juce::jmax (0.0, next.duration) : 0.0;
            const auto position = std::isfinite (next.position) ? juce::jlimit (0.0, duration, next.position) : 0.0;
            centre.nowPlayingInfo = @{
                MPMediaItemPropertyTitle: [NSString stringWithUTF8String: title.toRawUTF8()],
                MPMediaItemPropertyPlaybackDuration: @(duration),
                MPNowPlayingInfoPropertyElapsedPlaybackTime: @(position),
                MPNowPlayingInfoPropertyPlaybackRate: @(next.playing ? 1.0 : 0.0),
                MPNowPlayingInfoPropertyMediaType: @(MPNowPlayingInfoMediaTypeAudio)
            };
            centre.playbackState = next.playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
            published = true;
        }
    }
    std::function<State()> state;
    std::shared_ptr<Commands> commands { std::make_shared<Commands>() };
    std::vector<std::pair<MPRemoteCommand*, id>> targets;
    bool active = false, published = false;
};
MacMediaTransport::MacMediaTransport (std::function<State()> state, std::function<void(int)> command)
    : impl (std::make_unique<Impl> (std::move (state), std::move (command))) { startTimer (250); }
MacMediaTransport::~MacMediaTransport() { stopTimer(); }
void MacMediaTransport::timerCallback() { impl->update(); }
