/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <choc/gui/choc_WebView.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

// .getFloat64()��Int64�̎��ɃG���[��Ԃ��̂ŁA�ǂ������ɕ��򂷂�
double toFloat64(const choc::value::ValueView& value) {
	if (value.isInt64()) {
		return static_cast<double>(value.getInt64());
	}
	else {
		return value.getFloat64();
	}
}

std::string getMimeType(std::string const& ext)
{
	static std::unordered_map<std::string, std::string> mimeTypes{
	  { ".html",   "text/html" },
	  { ".js",     "application/javascript" },
	  { ".css",    "text/css" },
	  { ".json",   "application/json"},
	  { ".svg",    "image/svg+xml"},
	  { ".svgz",   "image/svg+xml"},
	};

	if (mimeTypes.count(ext) > 0)
	{
		return mimeTypes.at(ext);
	}

	return "application/octet-stream";
}

//==============================================================================
VVVSTAudioProcessorEditor::VVVSTAudioProcessorEditor(VVVSTAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p)
{
	choc::ui::WebView::Options options;
#ifdef JUCE_DEBUG
	options.enableDebugMode = true;
#else
	options.enableDebugMode = false;


	// ���[�g�p�X�i"/"�j�Ƃ��ėp����t�@�C���p�X��ݒ肷��B�����ł́A�v���O�����E�v���O�C���̃o�C�i���{�̂̃p�X�Ɠ���f�B���N�g���ɂ���i"WebView"�j�t�H���_�����[�g�p�X�i"/"�j�ɂ���
	auto asset_directory = juce::File::getSpecialLocation(juce::File::SpecialLocationType::currentExecutableFile).getSiblingFile("WebView");

	// WebView�̎��s���I�v�V�����̂P�Ƃ��āAWebView����`fetch`�v���ɑΉ�����R�[���o�b�N�֐�����������
	options.fetchResource = [this, assetDirectory = asset_directory](const choc::ui::WebView::Options::Path& path)
		-> std::optional<choc::ui::WebView::Options::Resource> {
		// WebView���烋�[�g�p�X�i"/"�j�̃��N�G�X�g���󂯂��ꍇ�A"/index.html"��WebView�ɒ񋟂���
		auto relative_path = "." + (path == "/" ? "/index.html" : path);
		auto file_to_read = assetDirectory.getChildFile(relative_path);

		// WebView�ɒ񋟂��郊�\�[�X�̓o�C�i���f�[�^�Ƃ��ēǂݍ���
		juce::MemoryBlock memory_block;
		if (!file_to_read.existsAsFile() || !file_to_read.loadFileAsData(memory_block))
			return {};

		// WebView��Web���\�[�X(�o�C�i���f�[�^+MIME�^�C�v)��Ԃ�
		return choc::ui::WebView::Options::Resource{
			std::vector<uint8_t>(memory_block.begin(), memory_block.end()),
			getMimeType(file_to_read.getFileExtension().toStdString())
		};
		};
#endif

	// CHOC��API����WebView�I�u�W�F�N�g�𐶐�����.�����ɂ͎��s���I�v�V������n��
	chocWebView = std::make_unique<choc::ui::WebView>(options);
#if JUCE_WINDOWS
	juceView = std::make_unique<juce::HWNDComponent>();
	juceView->setHWND(chocWebView->getViewHandle());
#elif JUCE_MAC
	juceView = std::make_unique<juce::NSViewComponent>();
	juceView->setView(chocWebView->getViewHandle());
#elif JUCE_LINUX
	juceView = std::make_unique<juce::XEmbedComponent>(chocWebView->getViewHandle());
#endif

	chocWebView->bind("vstGetMemory",
		[safe_this = juce::Component::SafePointer(this)](const choc::value::ValueView& args)
		-> choc::value::Value {
			std::string& memory = safe_this->audioProcessor.memory;

			return choc::value::createString(memory);
		});

	chocWebView->bind("vstSetMemory",
		[safe_this = juce::Component::SafePointer(this)](const choc::value::ValueView& args)
		-> choc::value::Value {
			const auto memory = args[0].getString();
			safe_this->audioProcessor.memory = memory;

			return choc::value::Value(0);
		});

	chocWebView->bind("vstGetConfig",
		[safe_this = juce::Component::SafePointer(this)](const choc::value::ValueView& args)
		-> choc::value::Value {
#ifdef JUCE_WINDOWS
			auto appData = std::getenv("APPDATA");
			auto path = std::string(appData) + "\\voicevox\\config.json";
#else
#error "Not implemented"
#endif
			std::ifstream ifs(path);
			std::string res;
			std::string str;

			if (ifs.fail()) {
				std::cerr << "Failed to open file." << std::endl;
				return choc::value::Value(-1);
			}
			while (getline(ifs, str)) {
				res += str;
			}
			return choc::value::createString(res);
		});

	chocWebView->bind("vstClearPhrases",
		[safe_this = juce::Component::SafePointer(this)](const choc::value::ValueView& args)
		-> choc::value::Value {
			safe_this->audioProcessor.phrases.clear();

			return choc::value::Value(0);
		});
	chocWebView->bind("vstUpdatePhrases",
		[safe_this = juce::Component::SafePointer(this)](const choc::value::ValueView& args)
		-> choc::value::Value {
			const auto removedPhrases = args[0];
			const auto changedPhrases = args[1];

			choc::audio::WAVAudioFileFormat<false> wavFileFormat;

			for (const auto& phraseId : removedPhrases) {
				auto id = std::string(phraseId.getString());
				safe_this->audioProcessor.phrases.erase(id);
			}

			for (const auto& phraseVal : changedPhrases) {
				auto wavB64 = phraseVal["wav"].getString();
				std::vector<uint8_t> wavBuffer;
				choc::base64::decodeToContainer(wavBuffer, wavB64);
				auto wavStream = new std::istringstream(std::string(wavBuffer.begin(), wavBuffer.end()));
				auto wavStreamPtr = std::shared_ptr<std::istream>(wavStream);
				auto reader = wavFileFormat.createReader(wavStreamPtr);

				if (reader == nullptr) {
					DBG("Failed to create reader");
					continue;
				}

				auto data = reader->loadFileContent(
					safe_this->audioProcessor.sampleRate
				);


				auto id = std::string(phraseVal["id"].getString());
				Phrase phrase(
					id,
					toFloat64(phraseVal["start"]),
					toFloat64(phraseVal["end"]),
					data
				);
				safe_this->audioProcessor.phrases.insert_or_assign(id, phrase);
			}



			return choc::value::Value(0);
		});

#ifdef JUCE_DEBUG
	chocWebView->navigate("http://localhost:5173");
#endif

	addAndMakeVisible(juceView.get());
	// Make sure that before the constructor has finished, you've set the
	// editor's size to whatever you need it to be.
	setResizable(true, true);
	setSize(800, 600);
}

VVVSTAudioProcessorEditor::~VVVSTAudioProcessorEditor()
{
	this->audioProcessor.removeActionListener(this);
}

//==============================================================================
void VVVSTAudioProcessorEditor::paint(juce::Graphics& g)
{
	// (Our component is opaque, so we must completely fill the background with a solid colour)
	// g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void VVVSTAudioProcessorEditor::resized()
{
	auto rect_ui = getLocalBounds();

	// WebView�̔z�u�i���W�ƃT�C�Y�̓K�p�j�����s����B
	juceView->setBounds(getLocalBounds());
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..
}

void VVVSTAudioProcessorEditor::actionListenerCallback(const juce::String& message)
{
	chocWebView->evaluateJavascript("window.vstOnMessage(" + message.toStdString() + ")");
}
