//--------------------------------------------------------------------------------------------------
//
// The general flow of this program is to:
//  1. Let the user pick an image file
//  2. Display the image full-screen
//  3. Let the user define a start and end box for a zoom effect using the mouse and QWER keys
//        Q + left-click: set starting top-left coordinate to current mouse position
//        W + left-click: set starting bottom-right
//        E + left-click: set ending top-left
//        R + left-click: set ending bottom-right
//  4. Hold down the space bar to zoom from the start coordinates to the end coordinates.
//
// If you want to record this zooming, open up a screen recorder like Open Broadcaster Software
// (available from http://obsproject.com/) and use this app as an input.  Be sure your monitor is
// in 1920x1080 resolution if you want a 1080p recording.
//
// Happy coding!
// @OgreYonder
//
// P.S. This code is entirely in the public domain.  Do whatever you want with it!
//
//--------------------------------------------------------------------------------------------------
#include <windows.h>    // Standard Windows header
#include <d3dx9.h>      // Extended functions for managing Direct3D
#include <d3d9.h>       // Basic Direct3D functionality

// Link required libraries
#pragma comment(lib,"d3d9.lib")
#ifdef _DEBUG
  #pragma comment(lib,"d3dx9d.lib")
#else
  #pragma comment(lib,"d3dx9.lib")
#endif

/**
 * Sets up the Direct3D device
 */
LPDIRECT3DDEVICE9 CreateD3DDevice(HWND hWnd,
                                  LPDIRECT3D9 pD3D,
                                  D3DPRESENT_PARAMETERS *pPresentationParameters) {

  // Set up the structure used to create the Direct3D device.
  D3DPRESENT_PARAMETERS d3dpp; 
  ZeroMemory(&d3dpp, sizeof(d3dpp));
  d3dpp.Windowed = TRUE;
  d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  d3dpp.EnableAutoDepthStencil = TRUE;
  d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
  d3dpp.hDeviceWindow = hWnd;
  d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE; // Lock to VSYNC

  // Create the device
  LPDIRECT3DDEVICE9 pd3dDevice;
  if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                &d3dpp, &pd3dDevice))) {
    return NULL;
  }

  // Copy the parameters structure
  CopyMemory(pPresentationParameters, &d3dpp, sizeof(d3dpp));

  // Return the device
  return pd3dDevice;
}

/**
 * Handles the Windows message pump
 */
BOOL HandleMessagePump(FLOAT *pElapsedTime) {
  static FLOAT last_frame_time = GetTickCount() / 1000.0f;

  // Used to process messages
  MSG msg;
  ZeroMemory(&msg, sizeof(msg));

  // Handle the Windows stuff
  while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    // Handle the message
    TranslateMessage(&msg);
    DispatchMessage(&msg);

    // Exit on a quit message
    if(msg.message == WM_QUIT) return false;
  }

  // Calculate the number of seconds since the last frame
  if(pElapsedTime) {
    float fTime = GetTickCount() / 1000.0f;
    *pElapsedTime = fTime - last_frame_time;
    last_frame_time = fTime;
  } else {
    last_frame_time = GetTickCount() / 1000.0f;
  }

  // Success
  return TRUE;
}


/**
 * Windows message handler.  Our version simply posts a quit message when the window is closed,
 * and gives all other messages to the default procedure.
 */
LRESULT WINAPI WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Exit when the window is closed
    if(uMsg == WM_CLOSE) {
      PostQuitMessage(0);
    } else {
      // Pass this message to the default processor
      return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }

    // Success
    return S_OK;
}

/**
 * Waits for a lost Direct3D device to return to a usable state, then resets the device using
 * the provided parameters.  Called after a lost device has been detected and all device-
 * dependant resources are unloaded.
 */
HRESULT WaitForLostDevice(LPDIRECT3DDEVICE9 pd3dDevice, D3DPRESENT_PARAMETERS * pD3DParams) {

    // Handle windows messages while waiting for a device return
    while(HandleMessagePump(NULL)) {
        if(D3DERR_DEVICENOTRESET == pd3dDevice->TestCooperativeLevel()) {
          // Reset the device
          if(SUCCEEDED(pd3dDevice->Reset(pD3DParams))) {
            return S_OK;
          } else {
            // Failed to reset the device
            return E_FAIL;
          }
        }
    }

    // Exit because the message pump was closed
    return E_FAIL;
}


bool OpenFileDialog(HWND hParent, const char* caption, const char* filter,
                    char* buffer, size_t bufferSize) {
    ZeroMemory(buffer, bufferSize);
    OPENFILENAME ofn = { sizeof(OPENFILENAME), hParent, NULL, filter, 
                         NULL, 0, 1, buffer, (DWORD)bufferSize, NULL, 0, NULL, caption,
                         OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_ENABLESIZING,
                         0, 0, 0, 0, 0, 0 };
    return TRUE == GetOpenFileName(&ofn);
}


//-------------------------------------------------------------------------------------------------
// This function is a bit tricky.
//
// First, top/left/bottom/right are both inputs and outputs.  The output values are the pixel
// positions on the image where the corners of the screen should be.  The input values are the
// pixel positions on the image of some input area.  The return value is the scaling factor
// of the image.  The returned coordinates are always both (height and width) the same scaling
// factor as the screen.
//
// If coords_in_dimensions == false, its effect is to contain the entire dimensions of
// the input area exactly within the screen of size screen_width screen_height.  The top/bottom and/
// or the left/right will touch the edges of the screen.  When pressing Q/W/E/R, this function is
// called for the entire image, with dimensions defined by top=0/left=0/bottom=height/right=width.
// 
// If coords_in_dimensions == true, this function contains the screen within the input dimensions.
// This is the mode used when zooming so that the screen doesn't accidentally go off the edge
// of the image.
//-------------------------------------------------------------------------------------------------
float PutScreenOverCoordinates(bool coords_in_dimensions, float *top, float *left, float *bottom, float *right, float screen_width, float screen_height) {
  float screen_aspect = screen_width / screen_height;
  float width = *right - *left;
  float height = *bottom - *top;
  float aspect = width / height;
  float scaling;
  if ((coords_in_dimensions && aspect > screen_aspect) || (!coords_in_dimensions && aspect < screen_aspect)) {
    // The image is more horizontal than the screen.  Will extend past the top/bottom.
    float offset = (width / screen_aspect - height) / 2;
    *top -= offset;
    *bottom += offset;
    //*bottom = *top + width / screen_aspect;
    scaling = width / screen_width;
  } else {
    // Screen is more horizontal than the image, so background bars are going to be on
    // the vertical sides.
    float offset = (height * screen_aspect - width) / 2;
    *left -= offset;
    *right += offset;
    //*right = *left + height * screen_aspect;
    scaling = height / screen_height;
  }

  return scaling;
}

//-------------------------------------------------------------------------------------------------
// Entry point to the app.  See the top of this file for description.
//-------------------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {

  // Structures used in the program
  HWND hWnd;
  D3DPRESENT_PARAMETERS d3dpp;
  LPDIRECT3D9 pD3D = NULL;
  LPDIRECT3DDEVICE9 pd3dDevice = NULL;
  LPDIRECT3DTEXTURE9 pTexture = NULL;
  FLOAT fElapsedTime;
  D3DXVECTOR3 vCamera(0.5f, 0.5f, 10.0f), vCameraLookAt(0.5f, 0.5f, 0.0f);

  CHAR imagePath[MAX_PATH];
  if (!OpenFileDialog(NULL, "Select Image File", "Image Files (*.JPG; *.JPEG; *.PNG; *.BMP; *.DDS)\0*.JPG;*.JPEG;*.PNG;*.BMP;*.DDS\0\0", imagePath, MAX_PATH)) {
      return 0;
  }

  // Register a standard window class
  WNDCLASS wc = { 0, WndProc, 0, 0, hInstance,
                  NULL,
                  LoadCursor(NULL, IDC_ARROW),
                  (HBRUSH)GetStockObject(WHITE_BRUSH),
                  NULL, "wnd_pzi" };
  RegisterClass(&wc);

  // Create a window
  if (NULL != (hWnd = CreateWindow(wc.lpszClassName, "Pan-Zoom Image", WS_POPUP | WS_SYSMENU | WS_VISIBLE,
                                   CW_USEDEFAULT, CW_USEDEFAULT, GetSystemMetrics(SM_CXSCREEN),
                                   GetSystemMetrics(SM_CYSCREEN), GetDesktopWindow(), NULL,
                                   hInstance, NULL)) &&
      NULL != (pD3D = Direct3DCreate9(D3D_SDK_VERSION)) &&
      NULL != (pd3dDevice = CreateD3DDevice(hWnd, pD3D, &d3dpp))) {

    // Get the current display mode
    D3DDISPLAYMODE d3ddm;
    if (FAILED(pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm))) {
        return NULL;
    }

    D3DXIMAGE_INFO imageInfo;
    D3DXGetImageInfoFromFile(imagePath, &imageInfo);

    float screen_width = (float)d3ddm.Width,
          screen_height = (float)d3ddm.Height,
          image_width = (float)imageInfo.Width,
          image_height = (float)imageInfo.Height;

    if (SUCCEEDED(D3DXCreateTextureFromFile(pd3dDevice, imagePath, &pTexture))) {

      // Set rendering states
      pd3dDevice->SetRenderState(D3DRS_ZENABLE,  FALSE);
      pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
      pd3dDevice->SetRenderState(D3DRS_FOGENABLE,    FALSE);
      pd3dDevice->SetRenderState(D3DRS_DITHERENABLE, TRUE);
      pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

      // Set the filters for texture sampling and mipmapping
      pd3dDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_ANISOTROPIC);
      pd3dDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
      pd3dDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC);

      float start_x1 = 0,
            start_y1 = 0,
            start_x2 = image_width,
            start_y2 = image_height;

      float end_x1 = 0,
            end_y1 = 0,
            end_x2 = screen_width,
            end_y2 = screen_height;

      float time = 30.0f;

      float dx1 = (end_x1 - start_x1) / time,
            dx2 = (end_x2 - start_x2) / time,
            dy1 = (end_y1 - start_y1) / time,
            dy2 = (end_y2 - start_y2) / time;
      float left = start_x1, top = start_y1, right = start_x2, bottom = start_y2;

      bool first_loop = true, initialized = false;

      // This is the main application loop.  HandleMessagePump runs each loop to 
      while (HandleMessagePump(&fElapsedTime)) {

        // Exit on ESC key
        if (GetKeyState(VK_ESCAPE) & 0x80) break;

        if (SUCCEEDED(pd3dDevice->BeginScene())) {

          // When space-bar is held, run the zoom.
          if (GetKeyState(VK_SPACE) & 0x80) {

            // If we haven't updated the screen since the user last picked coordinates using Q/W/E/R,
            // do the calculations.
            if (!initialized) {
              initialized = true;
              PutScreenOverCoordinates(false, &start_y1, &start_x1, &start_y2, &start_x2, screen_width, screen_height);
              PutScreenOverCoordinates(false, &end_y1,   &end_x1,   &end_y2,   &end_x2, screen_width, screen_height);
              dx1 = (end_x1 - start_x1) / time;
              dx2 = (end_x2 - start_x2) / time;
              dy1 = (end_y1 - start_y1) / time;
              dy2 = (end_y2 - start_y2) / time;
              left = start_x1;
              top = start_y1;
              right = start_x2;
              bottom = start_y2;
            }

            // This is really lame.  Hold down a key to change the speed.
            if (GetKeyState('1') & 0x80)        { fElapsedTime *= 0.15f;
            } else if (GetKeyState('2') & 0x80) { fElapsedTime *= 0.25f;
            } else if (GetKeyState('3') & 0x80) { fElapsedTime *= 0.5f;
            } else if (GetKeyState('4') & 0x80) { fElapsedTime *= 0.6f;
            } else if (GetKeyState('5') & 0x80) { fElapsedTime *= 0.8f;
            } else if (GetKeyState('6') & 0x80) { fElapsedTime *= 1.2f;
            } else if (GetKeyState('7') & 0x80) { fElapsedTime *= 1.5f;
            } else if (GetKeyState('8') & 0x80) { fElapsedTime *= 1.8f;
            } else if (GetKeyState('9') & 0x80) { fElapsedTime *= 2.0f;
            } else if (GetKeyState('0') & 0x80) { fElapsedTime *= 2.5f; }

            // Advance the coordinates of the screen's corners based on the deltas
            left += dx1 * fElapsedTime;
            top += dy1 * fElapsedTime;
            right += dx2 * fElapsedTime;
            bottom += dy2 * fElapsedTime;
          }

          // Select the image
          pd3dDevice->SetTexture(0, pTexture);

          // Render vertices directly from a structure in system memory. This is not
          // good as a general-purpose way of drawing vertices, but what we are doing
          // doesn't tax the GPU at all so efficiency doesn't matter.
          struct {
            FLOAT x,y,z,rhw;
            FLOAT u, v;
          } vertices[] = {
            {0.0f,screen_height,0.5f,1,left/image_width,bottom/image_height},{0.0f,0.0f,0.5f,1,left/image_width,top/image_height},{screen_width,0.0f,0.5f,1,right/image_width,top/image_height},
            {0.0f,screen_height,0.5f,1,left/image_width,bottom/image_height},{screen_width,0.0f,0.5f,1,right/image_width,top/image_height},{screen_width,screen_height,0.5f,1,right/image_width,bottom/image_height}
          };
          pd3dDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
          pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, (void*)vertices, sizeof(FLOAT)*6);

          bool sp1 = 0x80 == (GetKeyState('Q') & 0x80),
               sp2 = 0x80 == (GetKeyState('W') & 0x80),
               ep1 = 0x80 == (GetKeyState('E') & 0x80),
               ep2 = 0x80 == (GetKeyState('R') & 0x80);

          if (first_loop||sp1||sp2||ep1||ep2) {

            // This flag is used to initialize the top/left/bottom/right variables on the first loop.
            // It just makes it so that we don't have to call the initialization function the same way
            // in multiple places.
            first_loop = false;

            // Start the animation over again the next time the user hits the space-bar.
            initialized = false;

            // Reset image location so that it is entirely on-screen.  If the image is more horizontal
            // than the screen, it will repeat on the top/bottom edges.  If it is more vertical, it will
            // repeat on the left/right edges.
            float scaling;
            top = 0;
            left = 0;
            right = image_width;
            bottom = image_height;
            scaling = PutScreenOverCoordinates(true, &top, &left, &bottom, &right, screen_width, screen_height);

            // This logic handles setting whichever coordinate is selected
            POINT pt;
            GetCursorPos(&pt);
            if (GetKeyState(VK_LBUTTON) & 0x80) {

              // Clear the screen to green when the user sets a coordinate to give them some feedback
              pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0,255,0), 1.0f, 0);

              if (sp1) {
                start_x1 = pt.x * scaling + left;
                start_y1 = pt.y * scaling + top;
              }
              if (sp2) {
                start_x2 = pt.x * scaling + left;
                start_y2 = pt.y * scaling + top;
              }
              if (ep1) {
                end_x1 = pt.x * scaling + left;
                end_y1 = pt.y * scaling + top;
              }
              if (ep2) {
                end_x2 = pt.x * scaling + left;
                end_y2 = pt.y * scaling + top;
              }
            }
          }

          // End scene rendering
          pd3dDevice->EndScene();
        }

        // Flip the scene to the monitor
        if (FAILED(pd3dDevice->Present(NULL, NULL, NULL, NULL))) {

          // Free the device-dependant objects
          pTexture->Release();

          // Wait for the device to return
          if (FAILED(WaitForLostDevice(pd3dDevice, &d3dpp)))
              break;

          // Reload the device objects
          if (FAILED(D3DXCreateTextureFromFile(pd3dDevice, imagePath, &pTexture))) break;
        }
      }
    }
  }

  // Release Direct3D resources
  if (pTexture)   pTexture->Release();
  if (pd3dDevice) pd3dDevice->Release();
  if (pD3D)       pD3D->Release();

  // Get rid of the window class
  DestroyWindow(hWnd);
  UnregisterClass(wc.lpszClassName, hInstance);

  // Success
  return S_OK;
}
