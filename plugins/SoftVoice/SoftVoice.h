/* SoftVoice preset/sample instrument for LMMS.
 */
#pragma once

#include "Instrument.h"
#include "InstrumentView.h"
#include "Sample.h"

namespace lmms {
enum class SpeechPersonality : int
{
    Male = 0,
    Female = 1,
    LargeMale = 2,
    Child = 3,
    GiantMale = 4,
    MellowFemale = 5,
    MellowMale = 6,
    CrispMale = 7,
    Fly = 8,
    Robotoid = 9,
    Martian = 10,
    Colossus = 11,
    FastFred = 12,
    OldWoman = 13,
    Munchkin = 14,
    Troll = 15,
    Nerd = 16,
    Milktoast = 17,
    Tipsy = 18,
    Choirboy = 19
};

enum class SpeechStyle : int
{
    Natural = 0,
    Monotone = 2,
    Sing = 3,
    Random = 4
};

enum class SpeechVoicingMode : int
{
    Normal = 0,
    Breathy = 1,
    Whispered = 2
};

class SoftVoice : public Instrument
{
    Q_OBJECT
public:
    explicit SoftVoice(InstrumentTrack* track);
    void playNote(NotePlayHandle* note, SampleFrame* buffer) override;
    void deleteNotePluginData(NotePlayHandle* note) override;
    void saveSettings(QDomDocument& doc, QDomElement& parent) override;
    void loadSettings(const QDomElement& element) override;
    QString nodeName() const override;
    gui::PluginView* instantiateView(QWidget* parent) override;
    float desiredReleaseTimeMs() const override { return 3.0f; }
    void loadFile(const QString& file) override;

    bool importSpeech(const QString& file, QString* error);
    bool exportSpeech(const QString& file, QString* error) const;
    bool setSample(const QString& file);
    bool synthesize(QString* error);

    QString text = "fa";
    SpeechPersonality personality = SpeechPersonality::GiantMale;
    SpeechStyle style = SpeechStyle::Monotone;
    SpeechVoicingMode mode = SpeechVoicingMode::Normal;
    int rate = 138;
    int pitch = 65;
    QString wavPath;

private:
    Sample m_sample;
};

namespace gui {
class SoftVoiceView : public InstrumentViewFixedSize
{
    Q_OBJECT
public:
    explicit SoftVoiceView(Instrument* instrument, QWidget* parent);
private:
    void modelChanged() override;
    SoftVoice* m_instrument;
};
} // namespace gui
} // namespace lmms
