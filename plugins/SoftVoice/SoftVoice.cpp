/* SoftVoice preset/sample instrument for LMMS.
 */
#include "SoftVoice.h"

#include <QComboBox>
#include <QDomElement>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpinBox>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QtGlobal>

#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include "embed.h"
#include "InstrumentTrack.h"
#include "LmmsCommonMacros.h"
#include "NotePlayHandle.h"
#include "SampleCache.h"
#include "PathUtil.h"
#include "plugin_export.h"

namespace lmms {

extern "C" {
Plugin::Descriptor PLUGIN_EXPORT softvoice_plugin_descriptor = {
    LMMS_STRINGIFY(PLUGIN_NAME),
    "SoftVoice Speech",
    QT_TRANSLATE_NOOP( "PluginBrowser", "SoftVoice speech synthesizer" ),
    "MegaPiggy <megapiggy@outerwildsmods.com>",
    0x0100,
    Plugin::Type::Instrument,
    new PluginPixmapLoader( "logo" ),
    "speech,wav",
    nullptr
};
}

SoftVoice::SoftVoice(InstrumentTrack* track) : Instrument(track, &softvoice_plugin_descriptor) {}

void SoftVoice::playNote(NotePlayHandle* note, SampleFrame* buffer)
{
    const auto frames = note->framesLeftForCurrentPeriod();
    const auto offset = note->noteOffset();
    if (!note->m_pluginData)
    {
        note->m_pluginData = new Sample::PlaybackState(AudioResampler::Mode::Linear);
    }
    if (!note->isFinished() && m_sample.sampleSize() > 0)
    {
        if (m_sample.play(buffer + offset, static_cast<Sample::PlaybackState*>(note->m_pluginData),
            frames, Sample::Loop::Off, DefaultBaseFreq / note->frequency()))
        {
            applyRelease(buffer, note);
        }
    }
}

void SoftVoice::deleteNotePluginData(NotePlayHandle* note)
{
    delete static_cast<Sample::PlaybackState*>(note->m_pluginData);
    note->m_pluginData = nullptr;
}

void SoftVoice::saveSettings(QDomDocument&, QDomElement& parent)
{
    parent.setAttribute("text", text);
    parent.setAttribute("personality", static_cast<int>(personality));
    parent.setAttribute("style", static_cast<int>(style));
    parent.setAttribute("mode", static_cast<int>(mode));
    parent.setAttribute("rate", rate);
    parent.setAttribute("pitch", pitch);
    parent.setAttribute("wav", wavPath);
}

void SoftVoice::loadSettings(const QDomElement& element)
{
    text = element.attribute("text", "");
    personality = static_cast<SpeechPersonality>(element.attribute("personality", "4").toInt());
    style = static_cast<SpeechStyle>(element.attribute("style", "2").toInt());
    mode = static_cast<SpeechVoicingMode>(element.attribute("mode", "0").toInt());
    rate = element.attribute("rate", "138").toInt();
    pitch = element.attribute("pitch", "65").toInt();
    const auto path = element.attribute("wav");
    if (!path.isEmpty()) { setSample(path); }
}

QString SoftVoice::nodeName() const { return softvoice_plugin_descriptor.name; }

gui::PluginView* SoftVoice::instantiateView(QWidget* parent)
{
    return new gui::SoftVoiceView(this, parent);
}

void SoftVoice::loadFile(const QString& path)
{
    if (path.endsWith(".speech", Qt::CaseInsensitive))
    {
        QString error;
        importSpeech(path, &error);
    }
    else { setSample(path); }
}

bool SoftVoice::setSample(const QString& path)
{
    auto buffer = SampleCache::instance().fetchBuffer(PathUtil::stringToPath(path));
    if (!buffer) { return false; }
    m_sample = Sample(std::move(buffer));
    wavPath = path;
    return true;
}

bool SoftVoice::importSpeech(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error) { *error = file.errorString(); }
        return false;
    }
    bool inSettings = false;
    bool foundSettings = false;
    QString newText = text;
    SpeechPersonality newPersonality = personality;
    SpeechStyle newStyle = style;
    SpeechVoicingMode newMode = mode;
    int newPitch = pitch, newRate = rate;
    QTextStream stream(&file);
    while (!stream.atEnd())
    {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith('[') && line.endsWith(']'))
        {
            inSettings = line.compare("[Speaker Settings]", Qt::CaseInsensitive) == 0;
            foundSettings |= inSettings;
            continue;
        }
        if (!inSettings) { continue; }
        const int equals = line.indexOf('=');
        if (equals < 0) { continue; }
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1);
        bool ok = false;
        if (key.compare("Text", Qt::CaseInsensitive) == 0) { newText = value; continue; }
        const int number = value.trimmed().toInt(&ok);
        if (!ok) { continue; }
        if (key == "Personality") { newPersonality = static_cast<SpeechPersonality>(number); }
        else if (key == "Style") { newStyle = static_cast<SpeechStyle>(number); }
        else if (key == "Pitch") { newPitch = number; }
        else if (key == "Rate") { newRate = number; }
        else if (key == "VoicingMode") { newMode = static_cast<SpeechVoicingMode>(number); }
    }
    if (!foundSettings)
    {
        if (error) { *error = tr("Missing [Speaker Settings] section"); }
        return false;
    }
    text = newText;
    personality = newPersonality;
    style = newStyle;
    pitch = newPitch;
    rate = newRate;
    mode = newMode;
    return true;
}

// Synthesize off the audio thread using a separate 64-bit helper process.
bool SoftVoice::synthesize(QString* error)
{
#ifndef Q_OS_WIN
    if (error) { *error = tr("SoftVoice synthesis is available only on Windows."); }
    return false;
#else
    if (text.trimmed().isEmpty())
    {
        if (error) { *error = tr("Enter some text first."); }
        return false;
    }
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QString executable = qEnvironmentVariable("SOFTVOICE_HOST_EXE",
        applicationDir + "/softvoice-host.exe");
    if (!QFileInfo::exists(executable))
    {
        if (error) { *error = tr("Cannot find softvoice-host.exe at %1.").arg(executable); }
        return false;
    }
    const QString engineDir = qEnvironmentVariable("SOFTVOICE_ENGINE_DIR", applicationDir);
    const QDir engineFolder(engineDir);
    if (engineDir.isEmpty() || !QFileInfo(engineFolder.filePath("svctl64.dll")).isFile()
        || !QFileInfo(engineFolder.filePath("sveng64.dll")).isFile())
    {
        if (error) { *error = tr("Cannot find svctl64.dll and sveng64.dll in %1.").arg(engineDir); }
        return false;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/softvoice-samples";
    if (!QDir().mkpath(dir))
    {
        if (error) { *error = tr("Unable to create the SoftVoice sample cache directory."); }
        return false;
    }
    const QString resultPath = dir + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav";
    const QStringList arguments = {engineDir, resultPath,
        QString::number(static_cast<int>(personality)),
        QString::number(static_cast<int>(style)),
        QString::number(static_cast<int>(mode)),
        QString::number(rate), QString::number(pitch), text};
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(5000))
    {
        if (error) { *error = tr("Cannot start the SoftVoice helper: %1").arg(process.errorString()); }
        return false;
    }
    if (!process.waitForFinished(20000))
    {
        process.kill();
        process.waitForFinished();
        if (error) { *error = tr("SoftVoice synthesis timed out."); }
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const auto details = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (error)
        {
            *error = details.isEmpty()
                ? tr("SoftVoice helper stopped (exit code %1).").arg(process.exitCode())
                : tr("SoftVoice failed: %1").arg(details);
        }
        return false;
    }
    if (!setSample(resultPath))
    {
        if (error) { *error = tr("SoftVoice generated audio, but LMMS couldn't load the WAV."); }
        return false;
    }
    return true;
#endif
}

bool SoftVoice::exportSpeech(const QString& path, QString* error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (error) { *error = file.errorString(); }
        return false;
    }
    QTextStream stream(&file);
    stream << "[Speaker Settings]\nStyle=" << static_cast<int>(style)
        << "\nPersonality=" << static_cast<int>(personality)
        << "\nPitch=" << pitch
        << "\nRate=" << rate
        << "\nVoicingMode=" << static_cast<int>(mode)
        << "\nText=" << text << '\n';
    return stream.status() == QTextStream::Ok;
}

namespace gui {
SoftVoiceView::SoftVoiceView(Instrument* instrument, QWidget* parent)
    : InstrumentViewFixedSize(instrument, parent), m_instrument(static_cast<SoftVoice*>(instrument))
{
    auto* viewLayout = new QVBoxLayout(this);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    viewLayout->addWidget(scrollArea);

    auto* content = new QWidget(scrollArea);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);
    scrollArea->setWidget(content);

    auto* form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setVerticalSpacing(4);
    root->addLayout(form);

    auto* textEdit = new QLineEdit(m_instrument->text, content);
    form->addRow(tr("Text"), textEdit);
    QObject::connect(textEdit, &QLineEdit::textChanged, this, [this](const QString& value) {
        m_instrument->text = value;
    });

    auto* personality = new QComboBox(content);
    const auto addPersonality = [personality](const QString& label, SpeechPersonality value) {
        personality->addItem(label, static_cast<int>(value));
    };
    addPersonality(tr("Male"), SpeechPersonality::Male);
    addPersonality(tr("Female"), SpeechPersonality::Female);
    addPersonality(tr("Large male"), SpeechPersonality::LargeMale);
    addPersonality(tr("Child"), SpeechPersonality::Child);
    addPersonality(tr("Giant male"), SpeechPersonality::GiantMale);
    addPersonality(tr("Mellow female"), SpeechPersonality::MellowFemale);
    addPersonality(tr("Mellow male"), SpeechPersonality::MellowMale);
    addPersonality(tr("Crisp male"), SpeechPersonality::CrispMale);
    addPersonality(tr("Fly"), SpeechPersonality::Fly);
    addPersonality(tr("Robotoid"), SpeechPersonality::Robotoid);
    addPersonality(tr("Martian"), SpeechPersonality::Martian);
    addPersonality(tr("Colossus"), SpeechPersonality::Colossus);
    addPersonality(tr("Fast fred"), SpeechPersonality::FastFred);
    addPersonality(tr("Old woman"), SpeechPersonality::OldWoman);
    addPersonality(tr("Munchkin"), SpeechPersonality::Munchkin);
    addPersonality(tr("Troll"), SpeechPersonality::Troll);
    addPersonality(tr("Nerd"), SpeechPersonality::Nerd);
    addPersonality(tr("Milktoast"), SpeechPersonality::Milktoast);
    addPersonality(tr("Tipsy"), SpeechPersonality::Tipsy);
    addPersonality(tr("Choirboy"), SpeechPersonality::Choirboy);
    personality->setCurrentIndex(personality->findData(static_cast<int>(m_instrument->personality)));
    form->addRow(tr("Personality"), personality);
    QObject::connect(personality, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, personality](int index) {
            if (index >= 0) { m_instrument->personality = static_cast<SpeechPersonality>(personality->itemData(index).toInt()); }
        });

    auto* style = new QComboBox(content);
    style->addItem(tr("Natural"), static_cast<int>(SpeechStyle::Natural));
    style->addItem(tr("Monotone"), static_cast<int>(SpeechStyle::Monotone));
    style->addItem(tr("Sing"), static_cast<int>(SpeechStyle::Sing));
    style->addItem(tr("Random"), static_cast<int>(SpeechStyle::Random));
    style->setCurrentIndex(style->findData(static_cast<int>(m_instrument->style)));
    form->addRow(tr("Style"), style);
    QObject::connect(style, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, style](int index) {
            if (index >= 0) { m_instrument->style = static_cast<SpeechStyle>(style->itemData(index).toInt()); }
        });

    auto* mode = new QComboBox(content);
    mode->addItem(tr("Normal"), static_cast<int>(SpeechVoicingMode::Normal));
    mode->addItem(tr("Breathy"), static_cast<int>(SpeechVoicingMode::Breathy));
    mode->addItem(tr("Whispered"), static_cast<int>(SpeechVoicingMode::Whispered));
    mode->setCurrentIndex(mode->findData(static_cast<int>(m_instrument->mode)));
    form->addRow(tr("Mode"), mode);
    QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, mode](int index) {
            if (index >= 0) { m_instrument->mode = static_cast<SpeechVoicingMode>(mode->itemData(index).toInt()); }
        });

    auto* rate = new QSpinBox(content);
    rate->setRange(1, 1000);
    rate->setValue(m_instrument->rate);
    form->addRow(tr("Rate (WPM)"), rate);
    QObject::connect(rate, QOverload<int>::of(&QSpinBox::valueChanged), this,
        [this](int value) { m_instrument->rate = value; });

    auto* pitch = new QSpinBox(content);
    pitch->setRange(1, 20000);
    pitch->setValue(m_instrument->pitch);
    pitch->setSuffix(" Hz");
    form->addRow(tr("Pitch"), pitch);
    QObject::connect(pitch, QOverload<int>::of(&QSpinBox::valueChanged), this,
        [this](int value) { m_instrument->pitch = value; });

    auto* buttons = new QGridLayout();
    root->addLayout(buttons);
    auto* loadPreset = new QPushButton(tr("Open Preset"), content);
    auto* savePreset = new QPushButton(tr("Save Preset"), content);
    auto* loadWav = new QPushButton(tr("Load WAV"), content);
    auto* generate = new QPushButton(tr("Generate"), content);
    buttons->addWidget(loadPreset, 0, 0);
    buttons->addWidget(savePreset, 0, 1);
    buttons->addWidget(loadWav, 1, 0);
    buttons->addWidget(generate, 1, 1);
    buttons->setColumnStretch(0, 1);
    buttons->setColumnStretch(1, 1);

    auto* status = new QLabel(content);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    root->addWidget(status);
    root->addStretch();
    QObject::connect(generate, &QPushButton::clicked, this, [this, status, generate]() {
        generate->setEnabled(false);
        status->setText(tr("Rendering speech..."));
        QString error;
        const bool ok = m_instrument->synthesize(&error);
        status->setText(ok ? tr("SoftVoice speech generated and loaded into LMMS.") : error);
        generate->setEnabled(true);
    });
    QObject::connect(loadPreset, &QPushButton::clicked, this, [this, textEdit, personality, style, mode, rate, pitch, status]() {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open speech preset"), {}, tr("Speech (*.speech)"));
        if (path.isEmpty()) { return; }
        QString error;
        if (!m_instrument->importSpeech(path, &error)) { status->setText(error); return; }
        textEdit->setText(m_instrument->text);
        personality->setCurrentIndex(personality->findData(static_cast<int>(m_instrument->personality)));
        style->setCurrentIndex(style->findData(static_cast<int>(m_instrument->style)));
        mode->setCurrentIndex(mode->findData(static_cast<int>(m_instrument->mode)));
        rate->setValue(m_instrument->rate);
        pitch->setValue(m_instrument->pitch);
        status->setText(tr("Imported %1 (click Generate to synthesize)").arg(QFileInfo(path).fileName()));
    });
    QObject::connect(savePreset, &QPushButton::clicked, this, [this, status]() {
        const auto path = QFileDialog::getSaveFileName(this, tr("Save speech preset"), {}, tr("Speech (*.speech)"));
        if (path.isEmpty()) { return; }
        QString error;
        if (!m_instrument->exportSpeech(path, &error)) { status->setText(error); }
        else { status->setText(tr("Preset saved")); }
    });
    QObject::connect(loadWav, &QPushButton::clicked, this, [this, status]() {
        const auto path = QFileDialog::getOpenFileName(this, tr("Open rendered speech"), {}, tr("Audio (*.wav *.flac *.ogg)"));
        if (path.isEmpty()) { return; }
        status->setText(m_instrument->setSample(path) ? tr("Loaded %1").arg(QFileInfo(path).fileName())
                                                       : tr("Unable to load sample"));
    });
}

void SoftVoiceView::modelChanged() {}
} // namespace gui

extern "C" {
PLUGIN_EXPORT Plugin* lmms_plugin_main(Model* model, void*)
{
    return new SoftVoice(static_cast<InstrumentTrack*>(model));
}
}
} // namespace lmms
