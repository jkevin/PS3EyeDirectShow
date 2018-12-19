#include <streams.h>
#include <strsafe.h>
#include "ps3eye.h"
#include "PS3EyeSourceFilter.h"

PS3EyePushPin::PS3EyePushPin(HRESULT *phr, CSource *pFilter, ps3eye::PS3EYECam::PS3EYERef device) :
	CSourceStream(NAME("PS3 Eye Source"), phr, pFilter, L"Out"),
	_device(device)
{
	LPVOID refClock;
	HRESULT r = CoCreateInstance(CLSID_SystemClock, NULL, CLSCTX_INPROC_SERVER, IID_IReferenceClock, &refClock);
	if ((SUCCEEDED(r))) {
		_refClock = (IReferenceClock *)refClock;
	}
	else {
		OutputDebugString(L"PS3EyePushPin: failed to create reference clock\n");
		_refClock = NULL;
	}
}

PS3EyePushPin::~PS3EyePushPin() {
	if(_refClock != NULL) _refClock->Release();
}

HRESULT PS3EyePushPin::CheckMediaType(const CMediaType *pMediaType)
{
	CheckPointer(pMediaType, E_POINTER);

	if (pMediaType->IsValid() && *pMediaType->Type() == MEDIATYPE_Video &&
		pMediaType->Subtype() != NULL && *pMediaType->Subtype() == MEDIASUBTYPE_RGB32) {
		if (*pMediaType->FormatType() == FORMAT_VideoInfo &&
			pMediaType->Format() != NULL && pMediaType->FormatLength() > 0) {
			VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)pMediaType->Format();
			if ((pvi->bmiHeader.biWidth == 640 && pvi->bmiHeader.biHeight == 480) ||
				(pvi->bmiHeader.biWidth == 320 && pvi->bmiHeader.biHeight == 240)) {
				if (pvi->bmiHeader.biBitCount == 32 && pvi->bmiHeader.biCompression == BI_RGB
					&& pvi->bmiHeader.biPlanes == 1) {
					int minTime = 10000000 / 70;
					int maxTime = 10000000 / 2;
					if (pvi->AvgTimePerFrame >= minTime && pvi->AvgTimePerFrame <= maxTime) {
						return S_OK;
					}
				}
			}
		}
	}

	return E_FAIL;
}

HRESULT PS3EyePushPin::GetMediaType(int iPosition, CMediaType *pMediaType) {
	if (_currentMediaType.IsValid()) {
		// SetFormat from IAMStreamConfig has been called, only allow that format
		CheckPointer(pMediaType, E_POINTER);
		if (iPosition != 0) return E_UNEXPECTED;
		*pMediaType = _currentMediaType;
		return S_OK;
	}
	else {
		return _GetMediaType(iPosition, pMediaType);
	}
}

HRESULT PS3EyePushPin::_GetMediaType(int iPosition, CMediaType *pMediaType) {
	if (iPosition < 0 || iPosition >= 6) return E_UNEXPECTED;
	CheckPointer(pMediaType, E_POINTER);

	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)pMediaType->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
	if (pvi == 0)
		return(E_OUTOFMEMORY);
	ZeroMemory(pvi, pMediaType->cbFormat);


	int fps = 10;
	if (iPosition / 3 == 0) {
		// 640x480, {30, 60, 15} fps
		pvi->bmiHeader.biWidth = 640;
		pvi->bmiHeader.biHeight = 480;
		fps = iPosition == 2 ? 15 : 30 * (iPosition+1);
	}
	else  {
		// 320x240, {30, 60, 15} fps
		pvi->bmiHeader.biWidth = 320;
		pvi->bmiHeader.biHeight = 240;

		fps = iPosition == 5 ? 15 : 30 * (iPosition-2);
	}

	pvi->AvgTimePerFrame = 10000000 / fps;

	pvi->bmiHeader.biBitCount = 32;
	pvi->bmiHeader.biCompression = BI_RGB;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = GetBitmapSize(&pvi->bmiHeader);
	pvi->bmiHeader.biClrImportant = 0;

	SetRectEmpty(&(pvi->rcSource)); // we want the whole image area rendered.
	SetRectEmpty(&(pvi->rcTarget)); // no particular destination rectangle

	pMediaType->SetType(&MEDIATYPE_Video);
	pMediaType->SetFormatType(&FORMAT_VideoInfo);
	pMediaType->SetTemporalCompression(FALSE);

	// Work out the GUID for the subtype from the header info.
	const GUID SubTypeGUID = GetBitmapSubtype(&pvi->bmiHeader);
	pMediaType->SetSubtype(&SubTypeGUID);
	pMediaType->SetSampleSize(pvi->bmiHeader.biSizeImage);
	
	return NOERROR;
}

HRESULT PS3EyePushPin::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest)
{
	HRESULT hr;
	CAutoLock cAutoLock(m_pFilter->pStateLock());

	CheckPointer(pAlloc, E_POINTER);
	CheckPointer(pRequest, E_POINTER);

	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)m_mt.Format();

	// Ensure a minimum number of buffers
	if (pRequest->cBuffers == 0)
	{
		pRequest->cBuffers = 2;
	}
	pRequest->cbBuffer = pvi->bmiHeader.biSizeImage;

	ALLOCATOR_PROPERTIES Actual;
	hr = pAlloc->SetProperties(pRequest, &Actual);
	if (FAILED(hr))
	{
		return hr;
	}

	// Is this allocator unsuitable?
	if (Actual.cbBuffer < pRequest->cbBuffer)
	{
		return E_FAIL;
	}

	return S_OK;
}

HRESULT PS3EyePushPin::OnThreadCreate()
{
	if (_refClock != NULL) _refClock->GetTime(&_startTime);
	
	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)m_mt.Format();
	int fps = 10000000 / ((int)pvi->AvgTimePerFrame);
	OutputDebugString(L"initing device\n");
	if (_device.use_count() > 0) {
		bool didInit = _device->init(pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight, fps, ps3eye::PS3EYECam::EOutputFormat::BGRA);
		if (didInit) {
			OutputDebugString(L"starting device\n");
			_device->setFlip(false, true);
			_device->setAutogain(true);
			_device->setAutoWhiteBalance(true);
			_device->start();
			OutputDebugString(L"done\n");
			return S_OK;
		}
		else {
			OutputDebugString(L"failed to init device\n");
			return E_FAIL;
		}
	}
	else {
		// no device found but we'll render a blank frame so press on
		return S_OK;
	}
}

HRESULT PS3EyePushPin::OnThreadDestroy()
{
	if (_device.use_count() > 0) {
		_device->stop();
	}
	return S_OK;
}

HRESULT PS3EyePushPin::FillBuffer(IMediaSample *pSample)
{
	BYTE *pData;
	long cbData;

	CheckPointer(pSample, E_POINTER);

	pSample->GetPointer(&pData);
	cbData = pSample->GetSize();

	// Check that we're still using video
	ASSERT(m_mt.formattype == FORMAT_VideoInfo);

	if (_device.use_count() > 0) {
		_device->getFrame(pData);
	}
	else {
		// TODO: fill with error message image
		for (int i = 0; i < cbData; ++i) pData[i] = 0;
	}

	if (_refClock != NULL) {
		VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)m_mt.Format();
		REFERENCE_TIME t;
		_refClock->GetTime(&t);
		REFERENCE_TIME rtStart = t - _startTime - pvi->AvgTimePerFrame; // compensate for frame buffer in PS3EYECam
		REFERENCE_TIME rtStop = rtStart + pvi->AvgTimePerFrame;

		pSample->SetTime(&rtStart, &rtStop);
		// Set TRUE on every sample for uncompressed frames
		pSample->SetSyncPoint(TRUE);
	}

	return S_OK;
}

HRESULT __stdcall PS3EyePushPin::Set(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData)
{
	if (guidPropSet == AMPROPSETID_Pin) {
		return E_PROP_ID_UNSUPPORTED;
	}
	else {
		return E_PROP_SET_UNSUPPORTED;
	}
}

HRESULT __stdcall PS3EyePushPin::Get(REFGUID guidPropSet, DWORD dwPropID, LPVOID pInstanceData, DWORD cbInstanceData, LPVOID pPropData, DWORD cbPropData, DWORD * pcbReturned)
{
	if (guidPropSet == AMPROPSETID_Pin) {
		if (dwPropID == AMPROPERTY_PIN_CATEGORY) {
			if (cbPropData >= sizeof(GUID)) {
				*((GUID *)pPropData) = PIN_CATEGORY_CAPTURE;
				return S_OK;
			}
			else {
				return E_POINTER;
			}
		}
		else {
			return E_PROP_ID_UNSUPPORTED;
		}
	}
	else {
		return E_PROP_SET_UNSUPPORTED;
	}
}

HRESULT __stdcall PS3EyePushPin::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD * pTypeSupport)
{
	if (guidPropSet == AMPROPSETID_Pin) {
		if (dwPropID == AMPROPERTY_PIN_CATEGORY) {
			*pTypeSupport = KSPROPERTY_SUPPORT_GET;
			return S_OK;
		}
		else {
			return E_PROP_ID_UNSUPPORTED;
		}
	}
	else {
		return E_PROP_SET_UNSUPPORTED;
	}
}

HRESULT __stdcall PS3EyePushPin::SetFormat(AM_MEDIA_TYPE * pmt)
{
	OutputDebugString(L"SETTING FORMAT\n");
	CheckPointer(pmt, E_POINTER)
	HRESULT hr = S_OK;
	CMediaType mt(*pmt, &hr);
	if (FAILED(hr)) return hr;

	if (SUCCEEDED(CheckMediaType(&mt))) {
		IPin *cpin;
		
		HRESULT hr2 = ConnectedTo(&cpin);
		if (SUCCEEDED(hr2)) {
			if (SUCCEEDED(cpin->QueryAccept(pmt))) {
				{
					CAutoLock cAutoLock(m_pFilter->pStateLock());
					CMediaType lastMT = _currentMediaType;
					_currentMediaType = mt;
				}
				
				hr = m_pFilter->GetFilterGraph()->Reconnect(cpin);
			}
			else {
				hr = E_FAIL;
			}
		}
		else {
			CAutoLock cAutoLock(m_pFilter->pStateLock());
			_currentMediaType = mt;
			hr = S_OK;
		}
		if (cpin != NULL) {
			cpin->Release();
		}
	}
	else {
		hr = E_FAIL;
	}
	return hr;
}

HRESULT __stdcall PS3EyePushPin::GetFormat(AM_MEDIA_TYPE ** ppmt)
{
	CheckPointer(ppmt, E_POINTER);
	*ppmt = NULL;
	if (!_currentMediaType.IsValid()) {
		CMediaType dmt;
		_GetMediaType(0, &dmt);
		*ppmt = CreateMediaType(&dmt);
	}
	else {
		*ppmt = CreateMediaType(&_currentMediaType);
	}
	if (*ppmt == NULL) {
		return E_FAIL;
	}
	else {
		return S_OK;
	}
}

HRESULT __stdcall PS3EyePushPin::GetNumberOfCapabilities(int * piCount, int * piSize)
{
	CheckPointer(piCount, E_POINTER);
	CheckPointer(piSize, E_POINTER);
	*piCount = 6;
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
	return S_OK;
}

HRESULT __stdcall PS3EyePushPin::GetStreamCaps(int iIndex, AM_MEDIA_TYPE ** ppmt, BYTE * pSCC)
{
	CheckPointer(ppmt, E_POINTER);
	CheckPointer(pSCC, E_POINTER);
	CMediaType mt;
	HRESULT hr = _GetMediaType(iIndex, &mt);
	if (SUCCEEDED(hr)) {
		*ppmt = CreateMediaType(&mt);
		VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER*)mt.Format();

		const SIZE inputSize = { pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight };
		VIDEO_STREAM_CONFIG_CAPS *cc = (VIDEO_STREAM_CONFIG_CAPS *)pSCC;
		cc->guid = MEDIATYPE_Video;
		cc->VideoStandard = 0;
		cc->InputSize = inputSize;
		cc->MinCroppingSize = inputSize;
		cc->MaxCroppingSize = inputSize;
		cc->CropGranularityX = 4;
		cc->CropGranularityY = 4;
		cc->CropAlignX = 4;
		cc->CropAlignY = 4;
		cc->MinOutputSize = inputSize;
		cc->MaxOutputSize = inputSize;
		cc->OutputGranularityX = 4;
		cc->OutputGranularityY = 4;
		cc->StretchTapsX = 0;
		cc->StretchTapsY = 0;
		cc->ShrinkTapsX = 0;
		cc->ShrinkTapsY = 0;
		cc->MinFrameInterval = 10000000 / 60;
		cc->MaxFrameInterval = 10000000 / 2;
		cc->MinBitsPerSecond = 0;
		cc->MaxBitsPerSecond = (LONG)1000000000000;
	}
	return hr;
}
