#include "http.hpp"
#include "nt.hpp"

#include <Windows.h>
#include <urlmon.h>

#pragma comment(lib, "urlmon.lib")

namespace utils::http
{
	std::optional<std::string> get_data(const std::string& url)
	{
		IStream* stream = nullptr;

		if (FAILED(URLOpenBlockingStreamA(nullptr, url.data(), &stream, 0, nullptr)))
		{
			return {};
		}

		char buffer[0x1000];
		std::string result;

		HRESULT status{};

		do
		{
			DWORD bytes_read = 0;
			status = stream->Read(buffer, sizeof(buffer), &bytes_read);

			if (bytes_read > 0)
			{
				result.append(buffer, bytes_read);
			}
		}
		while (SUCCEEDED(status) && status != S_FALSE);

		stream->Release();

		if (FAILED(status))
		{
			return {};
		}

		return {result};
	}

	std::future<std::optional<std::string>> get_data_async(const std::string& url)
	{
		return std::async(std::launch::async, [url]()
		{
			return get_data(url);
		});
	}
}
