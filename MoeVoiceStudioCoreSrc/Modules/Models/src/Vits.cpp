#include "../header/Vits.hpp"

#include <random>

INFERCLASSHEADER
Vits::~Vits()
{
	logger.log(L"[Info] unloading Vits Models");
	delete sessionDec;
	delete sessionDp;
	delete sessionEnc_p;
	delete sessionFlow;
	delete sessionEmb;
	sessionDec = nullptr;
	sessionDp = nullptr;
	sessionEnc_p = nullptr;
	sessionFlow = nullptr;
	sessionEmb = nullptr;
	logger.log(L"[Info] Vits Models unloaded");
}

Vits::Vits(const rapidjson::Document& _config, const callback& _cb, const callback_params& _mr,const DurationCallback& _dcbb)
{
	_modelType = modelType::Vits;

	//Check Folder
	if (_config["folder"].IsNull())
		throw std::exception("[Error] Missing field \"folder\" (Model Folder)");
	if (!_config["folder"].IsString())
		throw std::exception("[Error] Field \"folder\" (Model Folder) Must Be String");
	const auto _folder = to_wide_string(_config["folder"].GetString());
	if (_folder.empty())
		throw std::exception("[Error] Field \"folder\" (Model Folder) Can Not Be Empty");
	const std::wstring _path = GetCurrentFolder() + L"\\Models\\" + _folder + L"\\";

	//LoadModels
	try
	{
		logger.log(L"[Info] loading Vits Models");
		sessionDec = new Ort::Session(*env, (_path + L"_dec.onnx").c_str(), *session_options);
		sessionDp = new Ort::Session(*env, (_path + L"_dp.onnx").c_str(), *session_options);
		sessionEnc_p = new Ort::Session(*env, (_path + L"_enc_p.onnx").c_str(), *session_options);
		sessionFlow = new Ort::Session(*env, (_path + L"_flow.onnx").c_str(), *session_options);
		if (_waccess((_path + L"_emb.onnx").c_str(), 0) != -1)
			sessionEmb = new Ort::Session(*env, (_path + L"_emb.onnx").c_str(), *session_options);
		else
			sessionEmb = nullptr;
		logger.log(L"[Info] Vits Models loaded");
	}
	catch (Ort::Exception& _exception)
	{
		throw std::exception(_exception.what());
	}

	//Check SamplingRate
	if (_config["Rate"].IsNull())
		throw std::exception("[Error] Missing field \"Rate\" (SamplingRate)");
	if (_config["Rate"].IsInt() || _config["Rate"].IsInt64())
		_samplingRate = _config["Rate"].GetInt();
	else
		throw std::exception("[Error] Field \"Rate\" (SamplingRate) Must Be Int/Int64");

	//Check Symbol
	if (_config["Symbol"].IsNull())
		throw std::exception("[Error] Missing field \"Symbol\" (PhSymbol)");
	if (!_config["AddBlank"].IsNull())
		add_blank = _config["AddBlank"].GetBool();
	else
		logger.log(L"[Warn] Field \"AddBlank\" Is Missing, Use Default Value");

	//Load Symbol
	int64_t iter = 0;
	if (_config["Symbol"].IsArray())
	{
		logger.log(L"[Info] Use Phs");
		if (_config["Symbol"].Empty())
			throw std::exception("[Error] Field \"Symbol\" (PhSymbol) Can Not Be Empty");
		for (const auto& it : _config["Symbol"].GetArray())
			_Phs.insert({ to_wide_string(it.GetString()), iter++ });
		use_ph = true;
	}
	else
	{
		logger.log(L"[Info] Use Phs");
		const std::wstring SymbolsStr = to_wide_string(_config["Symbol"].GetString());
		if (SymbolsStr.empty())
			throw std::exception("[Error] Field \"Symbol\" (PhSymbol) Can Not Be Empty");
		for (auto it : SymbolsStr)
			_Symbols.insert(std::pair<wchar_t, int64_t>(it, iter++));
		use_ph = false;
	}

	if (!_config["Cleaner"].IsNull())
	{
		const auto Cleaner = to_wide_string(_config["Cleaner"].GetString());
		if (!Cleaner.empty())
			switch (_plugin.Load(Cleaner))
			{
			case (-1):
				throw std::exception("[Error] Plugin File Does Not Exist");
			case (1):
				throw std::exception("[Error] Plugin Has Some Error");
			default:
				logger.log(L"[Info] Plugin Loaded");
				break;
			}
		else
			logger.log(L"[Info] Disable Plugin");
	}
	else
		logger.log(L"[Info] Disable Plugin");

	_callback = _cb;
	_get_init_params = _mr;
	_dcb = _dcbb;
}

std::vector<std::vector<bool>> Vits::generatePath(float* duration, size_t durationSize, size_t maskSize)
{
	for (size_t i = 1; i < durationSize; ++i)
		duration[i] = duration[i - 1] + duration[i];
	std::vector<std::vector<bool>> path(durationSize, std::vector<bool>(maskSize, false));
	//const auto path = new float[maskSize * durationSize];
	/*
	for (size_t i = 0; i < maskSize; ++i)
		for (size_t j = 0; j < durationSize; ++j)
			path[i][j] = (j < (size_t)duration[i] ? 1.0f : 0.0f);
	for (size_t i = maskSize - 1; i > 0ull; --i)
		for (size_t j = 0; j < durationSize; ++j)
			path[i][j] -= path[i-1][j];
	 */
	auto dur = (size_t)duration[0];
	for (size_t j = 0; j < dur; ++j)
		path[j][0] = true;
	/*
	for (size_t i = maskSize - 1; i > 0ull; --i)
		for (size_t j = 0; j < durationSize; ++j)
			path[i][j] = (j < (size_t)duration[i] && j >= (size_t)duration[i - 1]);
	std::vector<std::vector<float>> tpath(durationSize, std::vector<float>(maskSize));
	for (size_t i = 0; i < maskSize; ++i)
		for (size_t j = 0; j < durationSize; ++j)
			tpath[j][i] = path[i][j];
	 */
	for (size_t j = maskSize - 1; j > 0ull; --j)
	{
		dur = (size_t)duration[j];
		for (auto i = (size_t)duration[j - 1]; i < dur; ++i)
			path[i][j] = true;
	}
	return path;
}

std::vector<int16_t> Vits::Infer(std::wstring& _inputLens)
{
	if (_inputLens.length() == 0)
	{
		int ret = InsertMessageToEmptyEditBox(_inputLens);
		if (ret == -1)
			throw std::exception("TTS Does Not Support Automatic Completion");
		if (ret == -2)
			throw std::exception("Please Select Files");
	}
	_inputLens += L'\n';
	std::vector<std::wstring> _Lens = CutLens(_inputLens);
	auto _configs = GetParam(_Lens);
	size_t proc = 0;
	_callback(proc, _Lens.size());
	std::vector<int16_t> _wavData;
	_wavData.reserve(441000);
	for (const auto& _input : _Lens)
	{
		if (_input.empty())
			continue;
		auto noise_scale = _configs[proc].noise_scale;
		auto noise_scale_w = _configs[proc].noise_scale_w;
		auto length_scale = _configs[proc].length_scale;
		if (noise_scale < 0.0f || noise_scale > 50.0f)
			noise_scale = 0.667f;
		if (noise_scale_w < 0.0f || noise_scale_w > 50.0f)
			noise_scale_w = 0.800f;
		if (length_scale < 0.01f || length_scale > 50.0f)
			length_scale = 1.000f;
		const auto seed = _configs[proc].seed;
		const auto chara = _configs[proc].chara;
		//
		std::mt19937 gen(static_cast<unsigned int>(seed));
		std::normal_distribution<float> normal(0, 1);

		std::vector<int64> text;
		text.reserve(_input.length() * 4 + 4);
		if (!use_ph)
		{
			for (auto it : _input)
			{
				if (add_blank)
					text.push_back(0);
				text.push_back(_Symbols[it]);
			}
			if (add_blank)
				text.push_back(0);
		}
		else
		{
			std::vector<std::wstring> textVec;
			textVec.reserve((_input.length() + 2) * 2);
			std::wstring _inputStrW = _input + L'|';
			while (!_inputStrW.empty())
			{
				const auto this_ph = _inputStrW.substr(0, _inputStrW.find(L'_'));
				if (add_blank)
					text.push_back(0);
				text.push_back(_Phs[this_ph]);
				const auto idx = _inputStrW.find(L'|');
				if (idx != std::wstring::npos)
					_inputStrW = _inputStrW.substr(idx + 1);
			}
			if (add_blank)
				text.push_back(0);
		}
		int64 textLength[] = { (int64)text.size() };
		std::vector<MTensor> outputTensors;
		std::vector<MTensor> inputTensors;
		const int64 inputShape1[2] = { 1,textLength[0] };
		constexpr int64 inputShape2[1] = { 1 };
		inputTensors.push_back(MTensor::CreateTensor<int64>(
			*memory_info, text.data(), textLength[0], inputShape1, 2));
		inputTensors.push_back(MTensor::CreateTensor<int64>(
			*memory_info, textLength, 1, inputShape2, 1));
		if (emo)
		{
			constexpr int64 inputShape3[1] = { 1024 };
			auto emoVec = GetEmotionVector(_configs[proc].emo);
			inputTensors.push_back(MTensor::CreateTensor<float>(
				*memory_info, emoVec.data(), 1024, inputShape3, 1));
			try
			{
				outputTensors = sessionEnc_p->Run(Ort::RunOptions{ nullptr },
					EnceInput.data(),
					inputTensors.data(),
					inputTensors.size(),
					EncOutput.data(),
					EncOutput.size());
			}
			catch (Ort::Exception& e)
			{
				throw std::exception((std::string("Locate: enc_p\n") + e.what()).c_str());
			}
		}
		else
		{
			try
			{
				outputTensors = sessionEnc_p->Run(Ort::RunOptions{ nullptr },
					EncInput.data(),
					inputTensors.data(),
					inputTensors.size(),
					EncOutput.data(),
					EncOutput.size());
			}
			catch (Ort::Exception& e)
			{
				throw std::exception((std::string("Locate: enc_p\n") + e.what()).c_str());
			}
		}
		std::vector<float>
			m_p(outputTensors[1].GetTensorData<float>(), outputTensors[1].GetTensorData<float>() + outputTensors[1].GetTensorTypeAndShapeInfo().GetElementCount()),
			logs_p(outputTensors[2].GetTensorData<float>(), outputTensors[2].GetTensorData<float>() + outputTensors[2].GetTensorTypeAndShapeInfo().GetElementCount()),
			x_mask(outputTensors[3].GetTensorData<float>(), outputTensors[3].GetTensorData<float>() + outputTensors[3].GetTensorTypeAndShapeInfo().GetElementCount());

		const auto xshape = outputTensors[0].GetTensorTypeAndShapeInfo().GetShape();
		std::vector<const char*> DpInput = DpInputTmp, FlowInput = FlowInputTmp, DecInput = DecInputTmp;
		std::vector<MTensor> inputSid;
		std::vector<MTensor> outputG;
		if (sessionEmb)
		{
			int64 Character[1] = { chara };
			inputSid.push_back(MTensor::CreateTensor<int64>(
				*memory_info, Character, 1, inputShape2, 1));
			try {
				outputG = sessionEmb->Run(Ort::RunOptions{ nullptr },
					EMBInput.data(),
					inputSid.data(),
					inputSid.size(),
					EMBOutput.data(),
					EMBOutput.size());
			}
			catch (Ort::Exception& e)
			{
				throw std::exception((std::string("Locate: emb\n") + e.what()).c_str());
			}
		}
		else
		{
			DpInput.pop_back();
			FlowInput.pop_back();
			DecInput.pop_back();
		}
		const int64 zinputShape[3] = { xshape[0],2,xshape[2] };
		const int64 zinputCount = xshape[0] * xshape[2] * 2;
		std::vector<float> zinput(zinputCount, 0.0);
		for (auto& it : zinput)
			it = normal(gen) * noise_scale_w;

		inputTensors.clear();
		inputTensors.push_back(std::move(outputTensors[0]));
		inputTensors.push_back(std::move(outputTensors[3]));
		inputTensors.push_back(MTensor::CreateTensor<float>(
			*memory_info, zinput.data(), zinputCount, zinputShape, 3));
		std::vector<int64> GShape;
		if (sessionEmb) {
			GShape = outputG[0].GetTensorTypeAndShapeInfo().GetShape();
			GShape.push_back(1);
			inputTensors.push_back(MTensor::CreateTensor<float>(
				*memory_info, outputG[0].GetTensorMutableData<float>(), outputG[0].GetTensorTypeAndShapeInfo().GetElementCount(), GShape.data(), 3));
		}
		try
		{
			outputTensors = sessionDp->Run(Ort::RunOptions{ nullptr },
				DpInput.data(),
				inputTensors.data(),
				inputTensors.size(),
				DpOutput.data(),
				DpOutput.size());
		}
		catch (Ort::Exception& e)
		{
			throw std::exception((std::string("Locate: dp\n") + e.what()).c_str());
		}
		const auto w_ceil = outputTensors[0].GetTensorMutableData<float>();
		const size_t w_ceilSize = outputTensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
		for (size_t i = 0; i < w_ceilSize; ++i)
			w_ceil[i] = ceil(exp(w_ceil[i]) * x_mask[i] * length_scale);
		const auto maskSize = x_mask.size();
		if (_configs[proc].cp)
		{
			/*
			std::vector<UISliderLayer::itemParam> dstValue;
			dstValue.reserve((_inputStr.length()));
			for (size_t itr = 0; itr < _inputStr.length(); ++itr)
				dstValue.emplace_back(UISliderLayer::itemParam(_inputStr[itr] + std::to_wstring((int)w_ceil[itr * 2 + 1]), 1.0f));
			auto callback = [&](const std::vector<float>& input)
			{
				if (add_blank)
					for (size_t itr = 0; itr < input.size(); ++itr)
						w_ceil[itr * 2 + 1] = ceil(w_ceil[itr * 2 + 1] * input[itr]);
				else
					for (size_t itr = 0; itr < input.size(); ++itr)
						w_ceil[itr] = ceil(w_ceil[itr] * input[itr]);
				_mainWindow->tts_layer->opened = false;
			};
			_mainWindow->tts_layer->ShowLayer(L"��������Duration�ı���", std::move(dstValue), callback);
			while (_mainWindow->tts_layer->opened)
				Sleep(200);
			 */
		}
		float y_length_f = 0.0;
		int64 y_length;
		for (size_t i = 0; i < w_ceilSize; ++i)
			y_length_f += w_ceil[i];
		if (y_length_f < 1.0f)
			y_length = 1;
		else
			y_length = (int64)y_length_f;
		auto attn = generatePath(w_ceil, y_length, maskSize);
		std::vector<std::vector<float>> logVec(192, std::vector<float>(y_length, 0.0f));
		std::vector<std::vector<float>> mpVec(192, std::vector<float>(y_length, 0.0f));
		std::vector<float> nlogs_pData(192 * y_length);
		for (size_t i = 0; i < static_cast<size_t>(y_length); ++i)
		{
			for (size_t j = 0; j < 192; ++j)
			{
				for (size_t k = 0; k < maskSize; k++)
				{
					if (attn[i][k])
					{
						mpVec[j][i] += m_p[j * maskSize + k];
						logVec[j][i] += logs_p[j * maskSize + k];
					}
				}
				nlogs_pData[j * y_length + i] = mpVec[j][i] + normal(gen) * exp(logVec[j][i]) * noise_scale;
			}
		}
		inputTensors.clear();
		std::vector<float> y_mask(y_length);
		for (size_t i = 0; i < static_cast<size_t>(y_length); ++i)
			y_mask[i] = 1.0f;
		const int64 zshape[3] = { 1,192,y_length };
		const int64 yshape[3] = { 1,1,y_length };
		try
		{
			inputTensors.push_back(MTensor::CreateTensor<float>(
				*memory_info, nlogs_pData.data(), 192 * y_length, zshape, 3));
			inputTensors.push_back(MTensor::CreateTensor<float>(
				*memory_info, y_mask.data(), y_length, yshape, 3));
			if (sessionEmb)
			{
				inputTensors.push_back(MTensor::CreateTensor<float>(
					*memory_info, outputG[0].GetTensorMutableData<float>(), outputG[0].GetTensorTypeAndShapeInfo().GetElementCount(), GShape.data(), 3));
			}
			outputTensors = sessionFlow->Run(Ort::RunOptions{ nullptr },
				FlowInput.data(),
				inputTensors.data(),
				inputTensors.size(),
				FlowOutput.data(),
				FlowOutput.size());
			inputTensors[0] = std::move(outputTensors[0]);
			if (sessionEmb)
				inputTensors[1] = std::move(inputTensors[2]);
			inputTensors.pop_back();
			outputTensors = sessionDec->Run(Ort::RunOptions{ nullptr },
				DecInput.data(),
				inputTensors.data(),
				inputTensors.size(),
				DecOutput.data(),
				DecOutput.size());
		}
		catch (Ort::Exception& e)
		{
			throw std::exception((std::string("Locate: dec & flow\n") + e.what()).c_str());
		}
		const auto shapeOut = outputTensors[0].GetTensorTypeAndShapeInfo().GetShape();
		const auto outData = outputTensors[0].GetTensorData<float>();
		for (int bbb = 0; bbb < shapeOut[2]; bbb++)
			_wavData.emplace_back(static_cast<int16_t>(outData[bbb] * 32768.0f));
		_callback(proc, _Lens.size());
	}
	return _wavData;
}
INFERCLASSEND