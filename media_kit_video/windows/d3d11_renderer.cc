// This file is a part of media_kit
// (https://github.com/media-kit/media-kit).
//
// Copyright © 2021 & onwards, Hitesh Kumar Saini <saini123hitesh@gmail.com>.
// All rights reserved.
// Use of this source code is governed by MIT license that can be found in the
// LICENSE file.

#include "d3d11_renderer.h"

#include <iostream>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

#define FAIL(message)                                           \
  std::cout << "media_kit: D3D11Renderer: Failure: " << message \
            << std::endl;                                       \
  return false

#define CHECK_HRESULT(message) \
  if (FAILED(hr)) {            \
    FAIL(message);             \
  }

int D3D11Renderer::instance_count_ = 0;

D3D11Renderer::D3D11Renderer(int32_t width, int32_t height)
    : width_(width), height_(height) {
  mutex_ = ::CreateMutex(NULL, FALSE, NULL);
  if (!CreateD3D11Device()) {
    throw std::runtime_error("Unable to create Direct3D 11 device.");
  }
  if (!CreateTexture()) {
    throw std::runtime_error("Unable to create Direct3D 11 texture.");
  }
  instance_count_++;
}

D3D11Renderer::~D3D11Renderer() {
  CleanUp(true);
  ::ReleaseMutex(mutex_);
  ::CloseHandle(mutex_);
  instance_count_--;
}

void D3D11Renderer::SetSize(int32_t width, int32_t height) {
  if (width == width_ && height == height_) {
    return;
  }
  width_ = width;
  height_ = height;
  CleanUp(false);
  CreateTexture();
}

void D3D11Renderer::CopyTexture() {
  ::WaitForSingleObject(mutex_, INFINITE);
  // The actual texture copying is handled by mpv's render context
  // We just need to ensure synchronization here
  if (d3d_11_device_context_ != nullptr) {
    d3d_11_device_context_->Flush();
  }
  ::ReleaseMutex(mutex_);
}

void D3D11Renderer::CleanUp(bool release_device) {
  // Release texture
  if (shared_texture_) {
    shared_texture_->Release();
    shared_texture_ = nullptr;
  }

  // Release device and context if the instance is being destroyed
  if (release_device) {
    if (d3d_11_device_context_) {
      d3d_11_device_context_->Release();
      d3d_11_device_context_ = nullptr;
    }
    if (d3d_11_device_) {
      d3d_11_device_->Release();
      d3d_11_device_ = nullptr;
    }
  }
}

bool D3D11Renderer::CreateD3D11Device() {
  if (d3d_11_device_ != nullptr) {
    return true;  // Already created
  }

  const D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
  };

  IDXGIAdapter* adapter = nullptr;
  D3D_DRIVER_TYPE driver_type = D3D_DRIVER_TYPE_UNKNOWN;

  // Automatically selecting adapter on Windows 10 RTM or greater
  if (Utils::IsWindows10RTMOrGreater()) {
    adapter = nullptr;
    driver_type = D3D_DRIVER_TYPE_HARDWARE;
  } else {
    IDXGIFactory* dxgi = nullptr;
    ::CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&dxgi);
    if (dxgi) {
      dxgi->EnumAdapters(0, &adapter);
      dxgi->Release();
    }
  }

  auto hr = ::D3D11CreateDevice(
      adapter, driver_type, 0, 0, feature_levels,
      sizeof(feature_levels) / sizeof(D3D_FEATURE_LEVEL), D3D11_SDK_VERSION,
      &d3d_11_device_, nullptr, &d3d_11_device_context_);

  CHECK_HRESULT("D3D11CreateDevice");

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device = nullptr;
  auto dxgi_device_success = d3d_11_device_->QueryInterface(
      __uuidof(IDXGIDevice), (void**)&dxgi_device);
  if (SUCCEEDED(dxgi_device_success) && dxgi_device != nullptr) {
    dxgi_device->SetGPUThreadPriority(5);  // Must be in interval [-7, 7]
  }

  auto level = d3d_11_device_->GetFeatureLevel();
  std::cout << "media_kit: D3D11Renderer: Direct3D Feature Level: "
            << (((unsigned)level) >> 12) << "_"
            << ((((unsigned)level) >> 8) & 0xf) << std::endl;

  return true;
}

bool D3D11Renderer::CreateTexture() {
  D3D11_TEXTURE2D_DESC texture_desc = {0};
  texture_desc.Width = width_;
  texture_desc.Height = height_;
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags = 0;
  texture_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  auto hr = d3d_11_device_->CreateTexture2D(&texture_desc, nullptr,
                                            &shared_texture_);
  CHECK_HRESULT("ID3D11Device::CreateTexture2D");

  Microsoft::WRL::ComPtr<IDXGIResource> resource;
  hr = shared_texture_.As(&resource);
  CHECK_HRESULT("ID3D11Texture2D::As");

  // Retrieve the shared HANDLE for interop with Flutter
  hr = resource->GetSharedHandle(&handle_);
  CHECK_HRESULT("IDXGIResource::GetSharedHandle");

  shared_texture_->AddRef();

  return true;
}
