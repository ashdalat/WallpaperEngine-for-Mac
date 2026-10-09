module;

export module wescene.pkg.parse:sound_parser;
import rstd;
import wavsen.audio;
import wescene.fs;
import wescene.scene;
import wescene.pkg.scene_obj;

export namespace owe
{

using rstd::sync::Arc;

class SoundParser {
public:
    static Arc<rstd::dyn<SceneSoundControl>> Parse(const wpscene::SoundObject&, fs::VFS&,
                                                   wavsen::audio::SoundManager&, Scene*);
};

} // namespace owe
