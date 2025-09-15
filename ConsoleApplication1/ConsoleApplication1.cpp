#include <windows.h>
#include <wrl/client.h>     // 用于 ComPtr 智能指针

#include <comdef.h>         // 用于 _com_error 和 ThrowIfFailed
#include "d3dx12.h"

#include <dxgi1_4.h>        // 用于 DXGI 接口（CreateDXGIFactory1, EnumWarpAdapter）
#include <d3d12.h>          // DirectX 12 核心功能
#include "GameTimer.h"
#include "d3dApp.h"
// 定义 ThrowIfFailed 宏
#define ThrowIfFailed(hr) if (FAILED(hr)) { throw _com_error(hr); }

// 链接所需的库
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

class Renderer {
    Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
    Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice;
    Microsoft::WRL::ComPtr<ID3D12Fence> mFence;

    UINT mRtvDescriptorSize = 0;
    UINT mDsvDescriptorSize = 0;
    UINT mCbvSrvDescriptorSize = 0;

    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    int mClientWidth = 800;
    int mClientHeight = 600;

    bool m4xMsaaState = false; // 4X MSAA enabled
    UINT m4xMsaaQuality = 0; // quality level of 4X MSAA

    static const int SwapChainBufferCount = 2; // double buffer

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
    Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

    HWND mhMainWnd = nullptr; // main window handle

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;

    int mCurrBackBuffer = 0;

    GameTimer mTimer;


public :
    void init(HWND mainWindowWnd) {

        mhMainWnd = mainWindowWnd;

        #if defined(DEBUG) || defined(_DEBUG)
        // Enable the D3D12 debug layer.
        {
            Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
            ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
            debugController->EnableDebugLayer();
        }
        #endif
        
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));
        // Try to create hardware device.
        HRESULT hardwareResult = D3D12CreateDevice(
            nullptr, // default adapter
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&md3dDevice));
        // Fallback to WARP device.
        if (FAILED(hardwareResult))
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter> pWarpAdapter;
            ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));
            ThrowIfFailed(D3D12CreateDevice(
                pWarpAdapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&md3dDevice)));
        }

        // Create the Fence and Descriptor Sizes

        ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));
        mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        
        // check msaa quality
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
        msQualityLevels.Format = mBackBufferFormat;
        msQualityLevels.SampleCount = 4;
        msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        msQualityLevels.NumQualityLevels = 0;
        ThrowIfFailed(md3dDevice->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &msQualityLevels,
            sizeof(msQualityLevels)));
        m4xMsaaQuality = msQualityLevels.NumQualityLevels;
        assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

        CreateCommandObjects();
        CreateSwapChain();
        CreateRtvAndDsvDescriptorHeaps();

        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());

        for (UINT i = 0; i < SwapChainBufferCount; i++)
        {
            // Get the ith buffer in the swap chain.
            ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
            // Create an RTV to it.
            md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
            // Next entry in heap.
            rtvHeapHandle.Offset(1, mRtvDescriptorSize);
        }

        // Create the depth/stencil buffer and view.
        D3D12_RESOURCE_DESC depthStencilDesc;
        depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthStencilDesc.Alignment = 0;
        depthStencilDesc.Width = mClientWidth;
        depthStencilDesc.Height = mClientHeight;
        depthStencilDesc.DepthOrArraySize = 1;
        depthStencilDesc.MipLevels = 1;
        depthStencilDesc.Format = mDepthStencilFormat;
        depthStencilDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
        depthStencilDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
        depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE optClear;
        optClear.Format = mDepthStencilFormat;
        optClear.DepthStencil.Depth = 1.0f;
        optClear.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES DSHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(md3dDevice->CreateCommittedResource(
            &DSHeapProperties,
            D3D12_HEAP_FLAG_NONE,
            &depthStencilDesc,
            D3D12_RESOURCE_STATE_COMMON,
            &optClear,
            IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

        // Create descriptor to mip level 0 of entire resource using the
        // format of the resource.
        md3dDevice->CreateDepthStencilView(
            mDepthStencilBuffer.Get(),
            nullptr,
            DepthStencilView());

        // Transition the resource from its initial state to be used as a depth buffer.
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mDepthStencilBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
        mCommandList->ResourceBarrier(
            1,
            &barrier);
    }

    void CreateCommandObjects()
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        ThrowIfFailed(md3dDevice->CreateCommandQueue(
            &queueDesc, IID_PPV_ARGS(&mCommandQueue)));

        ThrowIfFailed(md3dDevice->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));
        ThrowIfFailed(md3dDevice->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            mDirectCmdListAlloc.Get(), // Associated command allocator
            nullptr, // Initial PipelineStateObject
            IID_PPV_ARGS(mCommandList.GetAddressOf())));
        // Start off in a closed state. This is because the first time we
        // refer to the command list we will Reset it, and it needs to be
        // closed before calling Reset.
        mCommandList->Close();
    }

    void CreateSwapChain()
    {
        // Release the previous swapchain we will be recreating.
        mSwapChain.Reset();
        DXGI_SWAP_CHAIN_DESC sd;
        sd.BufferDesc.Width = mClientWidth;
        sd.BufferDesc.Height = mClientHeight;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferDesc.Format = mBackBufferFormat;
        sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
        sd.SampleDesc.Count = m4xMsaaState ? 4 : 1;
        sd.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = SwapChainBufferCount;
        sd.OutputWindow = mhMainWnd;
        sd.Windowed = true;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        // Note: Swap chain uses queue to perform flush.
        ThrowIfFailed(mdxgiFactory->CreateSwapChain(
            mCommandQueue.Get(),
            &sd,
            mSwapChain.GetAddressOf()));
    }

    void CreateRtvAndDsvDescriptorHeaps()
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
        rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        rtvHeapDesc.NodeMask = 0;
        ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
            &rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dsvHeapDesc.NodeMask = 0;
        ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
            &dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const
    {
        // CD3DX12 constructor to offset to the RTV of the current back buffer.
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            mRtvHeap->GetCPUDescriptorHandleForHeapStart(),// handle start
            mCurrBackBuffer, // index to offset
            mRtvDescriptorSize); // byte size of descriptor
    }
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const
    {
        return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
    }

    int Run()
    {
        MSG msg = { 0 };
        mTimer.Reset();
        while (msg.message != WM_QUIT)
        {
            // If there are Window messages then process them.
            if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            // Otherwise, do animation/game stuff.
            else
            {
                mTimer.Tick();
                //if (!mAppPaused)
                //{
                //    CalculateFrameStats();
                //    Update(mTimer);
                //    Draw(mTimer);
                //}
                //else
                //{
                //    Sleep(100);
                //}
            }
        }
        return (int)msg.wParam;
    }

};


HWND CreateMainWindow(HINSTANCE hInstance, int nShowCmd) {
    //窗口初始化描述结构体(WNDCLASS)
    WNDCLASS wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;	//当工作区宽高改变，则重新绘制窗口
    wc.lpfnWndProc = MainWndProc;	//指定窗口过程
    wc.cbClsExtra = 0;	//借助这两个字段来为当前应用分配额外的内存空间（这里不分配，所以置0）
    wc.cbWndExtra = 0;	//借助这两个字段来为当前应用分配额外的内存空间（这里不分配，所以置0）
    wc.hInstance = hInstance;	//应用程序实例句柄（由WinMain传入）
    wc.hIcon = LoadIcon(0, IDC_ARROW);	//使用默认的应用程序图标
    wc.hCursor = LoadCursor(0, IDC_ARROW);	//使用标准的鼠标指针样式
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);	//指定了白色背景画刷句柄
    wc.lpszMenuName = 0;	//没有菜单栏
    wc.lpszClassName = L"MainWnd";	//窗口名
    //窗口类注册失败
    if (!RegisterClass(&wc))
    {
        //消息框函数，参数1：消息框所属窗口句柄，可为NULL。参数2：消息框显示的文本信息。参数3：标题文本。参数4：消息框样式
        MessageBox(0, L"RegisterClass Failed", 0, 0);
        return 0;
    }

    //窗口类注册成功
    RECT R;	//裁剪矩形
    R.left = 0;
    R.top = 0;
    R.right = 1280;
    R.bottom = 720;
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);	//根据窗口的客户区大小计算窗口的大小
    int width = R.right - R.left;
    int hight = R.bottom - R.top;

    //创建窗口,返回布尔值
    HWND mhMainWnd = CreateWindow(L"MainWnd", L"DX12Initialize", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, hight, 0, 0, hInstance, 0);
    //窗口创建失败
    if (!mhMainWnd)
    {
        MessageBox(0, L"CreatWindow Failed", 0, 0);
        return 0;
    }
    //窗口创建成功,则显示并更新窗口
    ShowWindow(mhMainWnd, nShowCmd);
    UpdateWindow(mhMainWnd);
    return mhMainWnd;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HWND mainWindowWnd = CreateMainWindow(hInstance, nCmdShow);
    Renderer renderer;
    renderer.init(mainWindowWnd);
    renderer.Run();
}