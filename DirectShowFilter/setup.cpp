//------------------------------------------------------------------------------
// File: Setup.cpp
//
// Desc: DirectShow sample code - implementation of PushSource sample filters
//
// Copyright (c)  Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------

#include <streams.h>
#include <initguid.h>

#include "ps3eye.h"
#include "PS3EyeSourceFilter.h"
#include "PS3EyeGuids.h"

// Note: It is better to register no media types than to register a partial 
// media type (subtype == GUID_NULL) because that can slow down intelligent connect 
// for everyone else.

// For a specialized source filter like this, it is best to leave out the 
// AMOVIESETUP_FILTER altogether, so that the filter is not available for 
// intelligent connect. Instead, use the CLSID to create the filter or just 
// use 'new' in your application.


// Filter setup data
const AMOVIESETUP_MEDIATYPE sudOpPinTypes =
{
	&MEDIATYPE_Video,       // Major type
	&MEDIASUBTYPE_RGB24      // Minor type
};

const AMOVIESETUP_PIN sudOutputPinPS3Eye =
{
	L"Output",      // Obsolete, not used.
	FALSE,          // Is this pin rendered?
	TRUE,           // Is it an output pin?
	FALSE,          // Can the filter create zero instances?
	FALSE,          // Does the filter create multiple instances?
	&CLSID_NULL,    // Obsolete.
	NULL,           // Obsolete.
	1,              // Number of media types.
	&sudOpPinTypes  // Pointer to media types.
};

/*
const AMOVIESETUP_FILTER sudPushSourcePS3Eye =
{
	&CLSID_PS3EyeSource,    // Filter CLSID
	g_ps3PS3EyeSource,      // String name
	MERIT_NORMAL,       // Filter merit
	1,                      // Number pins
	&sudOutputPinPS3Eye     // Pin details
};
*/



CFactoryTemplate g_Templates[1] =
{
	{
	  g_ps3PS3EyeSource,                // Name
	  &CLSID_PS3EyeSource,              // CLSID
	  PS3EyeSource::CreateInstance,     // Method to create an instance of MyComponent
	  NULL,                             // Initialization function
	  //&sudPushSourcePS3Eye,           // Set-up information (for filters)
	  NULL
	},
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

const REGFILTERPINS2 sudOutputPinPS3Eye2 =
{
	REG_PINFLAG_B_OUTPUT,
	1,
	1,
	&sudOpPinTypes,
	0,
	NULL,
	&PIN_CATEGORY_CAPTURE
};

/*
const REGFILTER2 sudPushSourcePS3Eye = {
	1,
	MERIT_NORMAL,
	1,
	&sudOutputPinPS3Eye
};
*/

////////////////////////////////////////////////////////////////////////
//
// Exported entry points for registration and unregistration 
// (in this case they only call through to default implementations).
//
////////////////////////////////////////////////////////////////////////

STDAPI DllRegisterServer()
{
	HRESULT hr;
	IFilterMapper2 *pFM2 = NULL;

	hr = AMovieDllRegisterServer2(TRUE);
	if (FAILED(hr))
		return hr;

	hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
		IID_IFilterMapper2, (void **)&pFM2);

	if (FAILED(hr))
		return hr;

	REGFILTER2 sudPushSourcePS3Eye;
	sudPushSourcePS3Eye.dwVersion = 2;
	sudPushSourcePS3Eye.dwMerit = MERIT_NORMAL;
	sudPushSourcePS3Eye.cPins2 = 1;
	sudPushSourcePS3Eye.rgPins2 = &sudOutputPinPS3Eye2;

	hr = pFM2->RegisterFilter(
		CLSID_PS3EyeSource,                // Filter CLSID. 
		g_ps3PS3EyeSource,                       // Filter name.
		NULL,                            // Device moniker. 
		&CLSID_VideoInputDeviceCategory,  // Input device category.
		g_ps3PS3EyeSource,                       // Instance data.
		&sudPushSourcePS3Eye                    // Pointer to filter information.
	);

	pFM2->Release();
	return hr;
}

STDAPI DllUnregisterServer()
{
	HRESULT hr;
	IFilterMapper2 *pFM2 = NULL;

	hr = AMovieDllRegisterServer2(FALSE);
	if (FAILED(hr))
		return hr;

	hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
		IID_IFilterMapper2, (void **)&pFM2);

	if (FAILED(hr))
		return hr;

	hr = pFM2->UnregisterFilter(&CLSID_VideoInputDeviceCategory, g_ps3PS3EyeSource, CLSID_PS3EyeSource);

	pFM2->Release();
	return hr;
}

//
// DllEntryPoint
//
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  dwReason,
	LPVOID lpReserved)
{
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}

