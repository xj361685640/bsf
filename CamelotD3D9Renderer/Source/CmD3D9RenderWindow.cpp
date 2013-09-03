/*
-----------------------------------------------------------------------------
This source file is part of OGRE
(Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2011 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "CmD3D9RenderWindow.h"
#include "CmInput.h"
#include "CmCoreThread.h"
#include "CmViewport.h"
#include "CmException.h"
#include "CmD3D9RenderSystem.h"
#include "CmRenderSystem.h"
#include "CmBitwise.h"
#include "CmPlatformWndProc.h"
#include "CmD3D9DeviceManager.h"

namespace CamelotFramework
{
	// HACK: During the move/resize modal loop no mouse messages will be posted, which means we will
	// never receive a "mouse up" event, even though user had to release the mouse to stop the loop. But our GUI system
	// relies on mouse down being followed by mouse up otherwise things end start to break a bit. So here we simulate 
	// the mouse release. 
	// Note: This is possible because SendMessage won't return until user releases the mouse and modal loop is done.
	void HACK_SendLMBUpEvent()
	{
		gInput().simulateButtonUp(BC_MOUSE_LEFT);
	}

	D3D9RenderWindow::D3D9RenderWindow(const RENDER_WINDOW_DESC& desc, HINSTANCE instance)
        : RenderWindow(desc), mInstance(instance), mIsDepthBuffered(true)  
	{
		mDevice = NULL;
		mIsFullScreen = false;		
		mIsExternal = false;
		mHWnd = 0;
		mActive = false;		
		mClosed = false;
		mSwitchingFullscreen = false;
		mDisplayFrequency = 0;
		mDeviceValid = false;
	}

	D3D9RenderWindow::~D3D9RenderWindow()
	{ }

	void D3D9RenderWindow::initialize_internal()
	{
		HINSTANCE hInst = mInstance;

		mFSAAType = D3DMULTISAMPLE_NONE;
		mFSAAQuality = 0;
		mFSAA = mDesc.FSAA;
		mVSync = mDesc.vsync;
		mVSyncInterval = mDesc.vsyncInterval;

		HWND parentHWnd = 0;
		HWND externalHandle = 0;

		NameValuePairList::const_iterator opt;
		// parentWindowHandle		-> parentHWnd
		opt = mDesc.platformSpecific.find("parentWindowHandle");
		if(opt != mDesc.platformSpecific.end())
			parentHWnd = (HWND)parseUnsignedInt(opt->second);
		// externalWindowHandle		-> externalHandle
		opt = mDesc.platformSpecific.find("externalWindowHandle");
		if(opt != mDesc.platformSpecific.end())
			externalHandle = (HWND)parseUnsignedInt(opt->second);

		if (!externalHandle)
		{
			DWORD		dwStyle = WS_VISIBLE | WS_CLIPCHILDREN;
			DWORD		dwStyleEx = 0;
			HMONITOR    hMonitor = NULL;		
			MONITORINFO monitorInfo;
			RECT		rc;

			// If we specified which adapter we want to use - find it's monitor.
			if (mDesc.monitorIndex != -1)
			{
				IDirect3D9* direct3D9 = D3D9RenderSystem::getDirect3D9();

				for (UINT32 i=0; i < direct3D9->GetAdapterCount(); ++i)
				{
					if (i == mDesc.monitorIndex)
					{
						hMonitor = direct3D9->GetAdapterMonitor(i);
						break;
					}
				}				
			}

			// If we didn't specified the adapter index, or if it didn't find it
			if (hMonitor == NULL)
			{
				POINT windowAnchorPoint;

				// Fill in anchor point.
				windowAnchorPoint.x = mDesc.left;
				windowAnchorPoint.y = mDesc.top;

				// Get the nearest monitor to this window.
				hMonitor = MonitorFromPoint(windowAnchorPoint, MONITOR_DEFAULTTOPRIMARY);
			}

			// Get the target monitor info		
			memset(&monitorInfo, 0, sizeof(MONITORINFO));
			monitorInfo.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(hMonitor, &monitorInfo);


			unsigned int winWidth, winHeight;
			winWidth = mDesc.width;
			winHeight = mDesc.height;

			UINT32 left = mDesc.left;
			UINT32 top = mDesc.top;

			// No specified top left -> Center the window in the middle of the monitor
			if (left == -1 || top == -1)
			{				
				int screenw = monitorInfo.rcWork.right  - monitorInfo.rcWork.left;
				int screenh = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

				// clamp window dimensions to screen size
				int outerw = (int(winWidth) < screenw)? int(winWidth) : screenw;
				int outerh = (int(winHeight) < screenh)? int(winHeight) : screenh;

				if (left == -1)
					left = monitorInfo.rcWork.left + (screenw - outerw) / 2;
				else if (mDesc.monitorIndex != -1)
					left += monitorInfo.rcWork.left;

				if (top == -1)
					top = monitorInfo.rcWork.top + (screenh - outerh) / 2;
				else if (mDesc.monitorIndex != -1)
					top += monitorInfo.rcWork.top;
			}
			else if (mDesc.monitorIndex != -1)
			{
				left += monitorInfo.rcWork.left;
				top += monitorInfo.rcWork.top;
			}

			mWidth = mDesiredWidth = mDesc.width;
			mHeight = mDesiredHeight = mDesc.height;
			mTop = top;
			mLeft = left;

			if (mDesc.fullscreen)
			{
				dwStyleEx |= WS_EX_TOPMOST;
				dwStyle |= WS_POPUP;
				mTop = monitorInfo.rcMonitor.top;
				mLeft = monitorInfo.rcMonitor.left;		
			}
			else
			{
				if (parentHWnd)
				{
					if(mDesc.toolWindow)
						dwStyleEx = WS_EX_TOOLWINDOW;
					else
						dwStyle |= WS_CHILD;
				}

				if (!parentHWnd || mDesc.toolWindow)
				{
					if (mDesc.border == WindowBorder::None)
						dwStyle |= WS_POPUP;
					else if (mDesc.border == WindowBorder::Fixed)
						dwStyle |= WS_OVERLAPPED | WS_BORDER | WS_CAPTION |
						WS_SYSMENU | WS_MINIMIZEBOX;
					else
						dwStyle |= WS_OVERLAPPEDWINDOW;
				}

				_adjustWindow(mDesc.width, mDesc.height, dwStyle, &winWidth, &winHeight);

				if (!mDesc.outerDimensions)
				{
					// Calculate window dimensions required
					// to get the requested client area
					SetRect(&rc, 0, 0, mWidth, mHeight);
					AdjustWindowRect(&rc, dwStyle, false);
					mWidth = rc.right - rc.left;
					mHeight = rc.bottom - rc.top;

					// Clamp window rect to the nearest display monitor.
					if (mLeft < monitorInfo.rcWork.left)
						mLeft = monitorInfo.rcWork.left;		

					if (mTop < monitorInfo.rcWork.top)					
						mTop = monitorInfo.rcWork.top;					

					if (static_cast<int>(winWidth) > monitorInfo.rcWork.right - mLeft)					
						winWidth = monitorInfo.rcWork.right - mLeft;	

					if (static_cast<int>(winHeight) > monitorInfo.rcWork.bottom - mTop)					
						winHeight = monitorInfo.rcWork.bottom - mTop;										
				}
			}


			// Register the window class
			// NB allow 4 bytes of window data for D3D9RenderWindow pointer
			WNDCLASS wc = { 0, PlatformWndProc::_win32WndProc, 0, 0, hInst,
				LoadIcon(0, IDI_APPLICATION), LoadCursor(NULL, IDC_ARROW),
				(HBRUSH)GetStockObject(BLACK_BRUSH), 0, "D3D9Wnd" };
			RegisterClass(&wc);

			// Create our main window
			// Pass pointer to self
			mIsExternal = false;
			mHWnd = CreateWindowEx(dwStyleEx, "D3D9Wnd", mDesc.title.c_str(), dwStyle,
				mLeft, mTop, winWidth, winHeight, parentHWnd, 0, hInst, this);
			mStyle = dwStyle;
		}
		else
		{
			mHWnd = externalHandle;
			mIsExternal = true;
		}

		RECT rc;
		// top and left represent outer window coordinates
		GetWindowRect(mHWnd, &rc);
		mTop = rc.top;
		mLeft = rc.left;
		// width and height represent interior drawable area
		GetClientRect(mHWnd, &rc);
		mWidth = rc.right;
		mHeight = rc.bottom;

		mName = mDesc.title;
		mIsDepthBuffered = mDesc.depthBuffer;
		mIsFullScreen = mDesc.fullscreen;
		mColorDepth = mDesc.colorDepth;

		mActive = true;
		mClosed = false;

		D3D9RenderSystem* rs = static_cast<D3D9RenderSystem*>(RenderSystem::instancePtr());
		rs->registerWindow(*this);

		RenderWindow::initialize_internal();
	}

	void D3D9RenderWindow::destroy_internal()
	{
		if (mDevice != NULL)
		{
			mDevice->detachRenderWindow(this);
			mDevice = NULL;
		}

		if (mHWnd && !mIsExternal)
		{
			DestroyWindow(mHWnd);
		}

		mHWnd = 0;
		mActive = false;
		mClosed = true;

		RenderWindow::destroy_internal();
	}

	void D3D9RenderWindow::setFullscreen(bool fullScreen, UINT32 width, UINT32 height)
	{
		THROW_IF_NOT_CORE_THREAD;

		if (fullScreen != mIsFullScreen || width != mWidth || height != mHeight)
		{
			if (fullScreen != mIsFullScreen)
				mSwitchingFullscreen = true;

			mStyle = WS_VISIBLE | WS_CLIPCHILDREN;

			bool oldFullscreen = mIsFullScreen;
			mIsFullScreen = fullScreen;
			mWidth = mDesiredWidth = width;
			mHeight = mDesiredHeight = height;

			if (fullScreen)
			{
				mStyle |= WS_POPUP;
				
				// Get the nearest monitor to this window.
				HMONITOR hMonitor = MonitorFromWindow(mHWnd, MONITOR_DEFAULTTONEAREST);

				// Get monitor info	
				MONITORINFO monitorInfo;

				memset(&monitorInfo, 0, sizeof(MONITORINFO));
				monitorInfo.cbSize = sizeof(MONITORINFO);
				GetMonitorInfo(hMonitor, &monitorInfo);

				mTop = monitorInfo.rcMonitor.top;
				mLeft = monitorInfo.rcMonitor.left;				
				
				// need different ordering here

				if (oldFullscreen)
				{
					// was previously fullscreen, just changing the resolution
					SetWindowPos(mHWnd, HWND_TOPMOST, mLeft, mTop, width, height, SWP_NOACTIVATE);
				}
				else
				{
					SetWindowPos(mHWnd, HWND_TOPMOST, mLeft, mTop, width, height, SWP_NOACTIVATE);
					//MoveWindow(mHWnd, mLeft, mTop, mWidth, mHeight, FALSE);
					SetWindowLong(mHWnd, GWL_STYLE, mStyle);
					SetWindowPos(mHWnd, 0, 0,0, 0,0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
				}
			}
			else
			{
				mStyle |= WS_OVERLAPPEDWINDOW;
				// Calculate window dimensions required
				// to get the requested client area
				unsigned int winWidth, winHeight;
				_adjustWindow(mWidth, mHeight, mStyle, &winWidth, &winHeight);

				SetWindowLong(mHWnd, GWL_STYLE, mStyle);
				SetWindowPos(mHWnd, HWND_NOTOPMOST, 0, 0, winWidth, winHeight,
					SWP_DRAWFRAME | SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOACTIVATE);
				// Note that we also set the position in the restoreLostDevice method
				// via _finishSwitchingFullScreen
			}
						
			// Have to release & trigger device reset
			// NB don't use windowMovedOrResized since Win32 doesn't know
			// about the size change yet				
			mDevice->invalidate(this);

			// TODO - Notify viewports of resize
		}
	} 

	bool D3D9RenderWindow::isActive() const
	{
		if (isFullScreen())
			return isVisible();

		return mActive && isVisible();
	}

	bool D3D9RenderWindow::isVisible() const
	{
		return (mHWnd && !IsIconic(mHWnd));
	}

	void D3D9RenderWindow::move(INT32 top, INT32 left)
	{
		THROW_IF_NOT_CORE_THREAD;

		if (mHWnd && !mIsFullScreen)
		{
			mLeft = left;
			mTop = top;

			SetWindowPos(mHWnd, 0, top, left, 0, 0,
				SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	void D3D9RenderWindow::resize(UINT32 width, UINT32 height)
	{
		THROW_IF_NOT_CORE_THREAD;

		if (mHWnd && !mIsFullScreen)
		{
			mWidth = width;
			mHeight = height;

			unsigned int winWidth, winHeight;
			_adjustWindow(width, height, mStyle, &winWidth, &winHeight);

			SetWindowPos(mHWnd, 0, 0, 0, winWidth, winHeight,
				SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	void D3D9RenderWindow::getCustomAttribute( const String& name, void* pData ) const
	{
		// Valid attributes and their equvalent native functions:
		// D3DDEVICE			: getD3DDevice
		// WINDOW				: getWindowHandle

		if( name == "D3DDEVICE" )
		{
			IDirect3DDevice9* *pDev = (IDirect3DDevice9**)pData;
			*pDev = _getD3D9Device();
			return;
		}		
		else if( name == "WINDOW" )
		{
			HWND *pHwnd = (HWND*)pData;
			*pHwnd = _getWindowHandle();
			return;
		}
		else if( name == "isTexture" )
		{
			bool *b = reinterpret_cast< bool * >( pData );
			*b = false;

			return;
		}
		else if( name == "D3DZBUFFER" )
		{
			IDirect3DSurface9* *pSurf = (IDirect3DSurface9**)pData;
			*pSurf = mDevice->getDepthBuffer(this);
			return;
		}
		else if( name == "DDBACKBUFFER" )
		{
			IDirect3DSurface9* *pSurf = (IDirect3DSurface9**)pData;
			*pSurf = mDevice->getBackBuffer(this);
			return;
		}
		else if( name == "DDFRONTBUFFER" )
		{
			IDirect3DSurface9* *pSurf = (IDirect3DSurface9**)pData;
			*pSurf = mDevice->getBackBuffer(this);
			return;
		}
	}

	void D3D9RenderWindow::swapBuffers()
	{
		THROW_IF_NOT_CORE_THREAD;

		if (mDeviceValid)
			mDevice->present(this);		
	}

	Int2 D3D9RenderWindow::screenToWindowPos(const Int2& screenPos) const
	{
		POINT pos;
		pos.x = screenPos.x;
		pos.y = screenPos.y;

		ScreenToClient(mHWnd, &pos);
		return Int2(pos.x, pos.y);
	}

	Int2 D3D9RenderWindow::windowToScreenPos(const Int2& windowPos) const
	{
		POINT pos;
		pos.x = windowPos.x;
		pos.y = windowPos.y;

		ClientToScreen(mHWnd, &pos);
		return Int2(pos.x, pos.y);
	}

	void D3D9RenderWindow::copyToMemory(const PixelData &dst, FrameBuffer buffer)
	{
		THROW_IF_NOT_CORE_THREAD;

		mDevice->copyContentsToMemory(this, dst, buffer);
	}

	void D3D9RenderWindow::startResize(WindowResizeDirection direction)
	{
		WPARAM dir = HTLEFT;
		switch(direction)
		{
		case WindowResizeDirection::Left:
			dir = HTLEFT;
			break;
		case WindowResizeDirection::TopLeft:
			dir = HTTOPLEFT;
			break;
		case WindowResizeDirection::Top:
			dir = HTTOP;
			break;
		case WindowResizeDirection::TopRight:
			dir = HTTOPRIGHT;
			break;
		case WindowResizeDirection::Right:
			dir = HTRIGHT;
			break;
		case WindowResizeDirection::BottomRight:
			dir = HTBOTTOMRIGHT;
			break;
		case WindowResizeDirection::Bottom:
			dir = HTBOTTOM;
			break;
		case WindowResizeDirection::BottomLeft:
			dir = HTBOTTOMLEFT;
			break;
		}

		SendMessage(mHWnd, WM_NCLBUTTONDOWN, dir, 0);
		HACK_SendLMBUpEvent();
	}

	void D3D9RenderWindow::endResize()
	{

	}

	void D3D9RenderWindow::startMove()
	{
		SendMessage(mHWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
		HACK_SendLMBUpEvent();
	}

	void D3D9RenderWindow::endMove()
	{

	}

	void D3D9RenderWindow::_windowMovedOrResized()
	{
		THROW_IF_NOT_CORE_THREAD;

		if (!mHWnd || IsIconic(mHWnd))
			return;
	
		updateWindowRect();

		RenderWindow::_windowMovedOrResized();
	}

	/************************************************************************/
	/* 						D3D9 IMPLEMENTATION SPECIFIC                    */
	/************************************************************************/

	void D3D9RenderWindow::_adjustWindow(unsigned int clientWidth, unsigned int clientHeight, 
		DWORD style, unsigned int* winWidth, unsigned int* winHeight)
	{
		// NB only call this for non full screen
		RECT rc;
		SetRect(&rc, 0, 0, clientWidth, clientHeight);
		AdjustWindowRect(&rc, style, false);
		*winWidth = rc.right - rc.left;
		*winHeight = rc.bottom - rc.top;

		// adjust to monitor
		HMONITOR hMonitor = MonitorFromWindow(mHWnd, MONITOR_DEFAULTTONEAREST);

		// Get monitor info	
		MONITORINFO monitorInfo;

		memset(&monitorInfo, 0, sizeof(MONITORINFO));
		monitorInfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(hMonitor, &monitorInfo);

		LONG maxW = monitorInfo.rcWork.right  - monitorInfo.rcWork.left;
		LONG maxH = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

		if (*winWidth > (unsigned int)maxW)
			*winWidth = maxW;
		if (*winHeight > (unsigned int)maxH)
			*winHeight = maxH;

	}

	void D3D9RenderWindow::_finishSwitchingFullscreen()
	{		
		if(mIsFullScreen)
		{
			// Need to reset the region on the window sometimes, when the 
			// windowed mode was constrained by desktop 
			HRGN hRgn = CreateRectRgn(0,0,mWidth, mHeight);
			SetWindowRgn(mHWnd, hRgn, FALSE);
		}
		else
		{
			// When switching back to windowed mode, need to reset window size 
			// after device has been restored
			// We may have had a resize event which polluted our desired sizes
			unsigned int winWidth, winHeight;
			_adjustWindow(mDesiredWidth, mDesiredHeight, mStyle, &winWidth, &winHeight);

			// deal with centreing when switching down to smaller resolution

			HMONITOR hMonitor = MonitorFromWindow(mHWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO monitorInfo;
			memset(&monitorInfo, 0, sizeof(MONITORINFO));
			monitorInfo.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(hMonitor, &monitorInfo);

			LONG screenw = monitorInfo.rcWork.right  - monitorInfo.rcWork.left;
			LONG screenh = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;


			int left = screenw > int(winWidth) ? ((screenw - int(winWidth)) / 2) : 0;
			int top = screenh > int(winHeight) ? ((screenh - int(winHeight)) / 2) : 0;
			SetWindowPos(mHWnd, HWND_NOTOPMOST, left, top, winWidth, winHeight,
				SWP_DRAWFRAME | SWP_FRAMECHANGED | SWP_NOACTIVATE);

			if (mWidth != mDesiredWidth ||
				mHeight != mDesiredHeight)
			{
				mWidth = mDesiredWidth;
				mHeight = mDesiredHeight;

				// TODO - Notify viewports of resize		
			}
		}
		mSwitchingFullscreen = false;
	}
	
	void D3D9RenderWindow::_buildPresentParameters(D3DPRESENT_PARAMETERS* presentParams) const
	{		
		// Set up the presentation parameters		
		IDirect3D9* pD3D = D3D9RenderSystem::getDirect3D9();
		D3DDEVTYPE devType = D3DDEVTYPE_HAL;

		if (mDevice != NULL)		
			devType = mDevice->getDeviceType();		
	

		ZeroMemory( presentParams, sizeof(D3DPRESENT_PARAMETERS) );
		presentParams->Windowed					= !mIsFullScreen;
		presentParams->SwapEffect				= D3DSWAPEFFECT_DISCARD;
		// triple buffer if VSync is on
		presentParams->BackBufferCount			= mVSync ? 2 : 1;
		presentParams->EnableAutoDepthStencil	= mIsDepthBuffered;
		presentParams->hDeviceWindow			= mHWnd;
		presentParams->BackBufferWidth			= mWidth;
		presentParams->BackBufferHeight			= mHeight;
		presentParams->FullScreen_RefreshRateInHz = mIsFullScreen ? mDisplayFrequency : 0;
		
		if (presentParams->BackBufferWidth == 0)		
			presentParams->BackBufferWidth = 1;					

		if (presentParams->BackBufferHeight == 0)	
			presentParams->BackBufferHeight = 1;					


		if (mVSync)
		{
			// D3D9 only seems to support 2-4 presentation intervals in fullscreen
			if (mIsFullScreen)
			{
				switch(mVSyncInterval)
				{
				case 1:
				default:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
					break;
				case 2:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_TWO;
					break;
				case 3:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_THREE;
					break;
				case 4:
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_FOUR;
					break;
				};
				// check that the interval was supported, revert to 1 to be safe otherwise
				D3DCAPS9 caps;
				pD3D->GetDeviceCaps(mDevice->getAdapterNumber(), devType, &caps);
				if (!(caps.PresentationIntervals & presentParams->PresentationInterval))
					presentParams->PresentationInterval = D3DPRESENT_INTERVAL_ONE;

			}
			else
			{
				presentParams->PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			}

		}
		else
		{
			// NB not using vsync in windowed mode in D3D9 can cause jerking at low 
			// frame rates no matter what buffering modes are used (odd - perhaps a
			// timer issue in D3D9 since GL doesn't suffer from this) 
			// low is < 200fps in this context
			if (!mIsFullScreen)
			{
				// TODO LOG PORT - Enable this warning later?
				//LogManager::getSingleton().logMessage("D3D9 : WARNING - "
				//	"disabling VSync in windowed mode can cause timing issues at lower "
				//	"frame rates, turn VSync on if you observe this problem.");
			}
			presentParams->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
		}

		presentParams->BackBufferFormat		= D3DFMT_R5G6B5;
		if( mColorDepth > 16 )
			presentParams->BackBufferFormat = D3DFMT_X8R8G8B8;

		if (mColorDepth > 16 )
		{
			// Try to create a 32-bit depth, 8-bit stencil
			if( FAILED( pD3D->CheckDeviceFormat(mDevice->getAdapterNumber(),
				devType,  presentParams->BackBufferFormat,  D3DUSAGE_DEPTHSTENCIL, 
				D3DRTYPE_SURFACE, D3DFMT_D24S8 )))
			{
				// Bugger, no 8-bit hardware stencil, just try 32-bit zbuffer 
				if( FAILED( pD3D->CheckDeviceFormat(mDevice->getAdapterNumber(),
					devType,  presentParams->BackBufferFormat,  D3DUSAGE_DEPTHSTENCIL, 
					D3DRTYPE_SURFACE, D3DFMT_D32 )))
				{
					// Jeez, what a naff card. Fall back on 16-bit depth buffering
					presentParams->AutoDepthStencilFormat = D3DFMT_D16;
				}
				else
					presentParams->AutoDepthStencilFormat = D3DFMT_D32;
			}
			else
			{
				// Woohoo!
				if( SUCCEEDED( pD3D->CheckDepthStencilMatch( mDevice->getAdapterNumber(), devType,
					presentParams->BackBufferFormat, presentParams->BackBufferFormat, D3DFMT_D24S8 ) ) )
				{
					presentParams->AutoDepthStencilFormat = D3DFMT_D24S8; 
				} 
				else 
					presentParams->AutoDepthStencilFormat = D3DFMT_D24X8; 
			}
		}
		else
			// 16-bit depth, software stencil
			presentParams->AutoDepthStencilFormat	= D3DFMT_D16;

		D3D9RenderSystem* rsys = static_cast<D3D9RenderSystem*>(CamelotFramework::RenderSystem::instancePtr());

		D3DMULTISAMPLE_TYPE fsaaType;
		DWORD fsaaQuality;

		rsys->determineFSAASettings(mDevice->getD3D9Device(),
			mFSAA, mFSAAHint, presentParams->BackBufferFormat, mIsFullScreen, 
			&fsaaType, &fsaaQuality);

		presentParams->MultiSampleType = fsaaType;
		presentParams->MultiSampleQuality = (fsaaQuality == 0) ? 0 : fsaaQuality;

		// Check sRGB
		if (mHwGamma)
		{
			/* hmm, this never succeeds even when device does support??
			if(FAILED(pD3D->CheckDeviceFormat(mDriver->getAdapterNumber(),
				devType, presentParams->BackBufferFormat, D3DUSAGE_QUERY_SRGBWRITE, 
				D3DRTYPE_SURFACE, presentParams->BackBufferFormat )))
			{
				// disable - not supported
				mHwGamma = false;
			}
			*/

		}
	}

	IDirect3DDevice9* D3D9RenderWindow::_getD3D9Device() const
	{
		return mDevice->getD3D9Device();
	}

	IDirect3DSurface9* D3D9RenderWindow::_getRenderSurface() const 
	{
		return mDevice->getBackBuffer(this);
	}

	bool D3D9RenderWindow::_getSwitchingFullscreen() const
	{
		return mSwitchingFullscreen;
	}

	D3D9Device* D3D9RenderWindow::_getDevice() const
	{
		return mDevice;
	}

	void D3D9RenderWindow::_setDevice(D3D9Device* device)
	{
		mDevice = device;
		mDeviceValid = false;
	}

	bool D3D9RenderWindow::_isDepthBuffered() const
	{
		return mIsDepthBuffered;
	}

	void D3D9RenderWindow::updateWindowRect()
	{
		RECT rc;
		BOOL result;

		// Update top left parameters
		result = GetWindowRect(mHWnd, &rc);
		if (result == FALSE)
		{
			mTop = 0;
			mLeft = 0;
			mWidth = 0;
			mHeight = 0;
			return;
		}
		
		mTop = rc.top;
		mLeft = rc.left;

		// width and height represent drawable area only
		result = GetClientRect(mHWnd, &rc);
		if (result == FALSE)
		{
			mTop = 0;
			mLeft = 0;
			mWidth = 0;
			mHeight = 0;
			return;
		}

		mWidth  = rc.right - rc.left;
		mHeight = rc.bottom - rc.top;	
	}

	bool D3D9RenderWindow::_validateDevice()
	{
		mDeviceValid = mDevice->validate(this);
		return mDeviceValid;
	}
}
