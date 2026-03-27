#pragma once
#include "pch.h"
namespace MiniCAD
{ 
    interface IDeviceNotify   // 设备丢失通知接口
    {
        virtual void OnDeviceLost() = 0;
        virtual void OnDeviceRestored() = 0;

    protected:
        ~IDeviceNotify() = default;
    };

	class Device
	{
	public: 
        Device(D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL_11_0) noexcept;
		~Device() = default; 

		Device(Device&&) = default;
		Device& operator= (Device&&) = default;

		Device(Device const&) = delete;
		Device& operator= (Device const&) = delete;

        void Initialize();       // 初始化       
        void HandleDeviceLost(); // Device Lost
        void RegisterDeviceNotify(IDeviceNotify* notify) noexcept {m_deviceNotify = notify; }

        ID3D11Device1*        GetDevice()       const noexcept { return m_d3dDevice.Get(); }
        ID3D11DeviceContext1* GetContext()      const noexcept { return m_d3dContext.Get(); }
        IDXGIFactory2*        GetFactory()      const noexcept { return m_dxgiFactory.Get(); }
        D3D_FEATURE_LEVEL     GetFeatureLevel() const noexcept { return m_featureLevel; }

         
        void PIXBeginEvent(const wchar_t* name)
        {
            if (m_annotation)
            {
                m_annotation->BeginEvent(name);
            }
        }

        void PIXEndEvent()
        {
            if (m_annotation)
            {
                m_annotation->EndEvent();
            }
        }

        void PIXSetMarker(const wchar_t* name)
        {
            if (m_annotation)
            {
                m_annotation->SetMarker(name);
            }
        }

    private:
        void CreateFactory();
        void CreateDevice();
        void GetHardwareAdapter(IDXGIAdapter1** adapter);


        Microsoft::WRL::ComPtr<IDXGIFactory2>              m_dxgiFactory;
        Microsoft::WRL::ComPtr<ID3D11Device1>              m_d3dDevice;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1>       m_d3dContext;
        Microsoft::WRL::ComPtr<ID3DUserDefinedAnnotation>  m_annotation;

        D3D_FEATURE_LEVEL  m_featureLevel;
        D3D_FEATURE_LEVEL  m_minFeatureLevel;
        IDeviceNotify*     m_deviceNotify;
	};
}
