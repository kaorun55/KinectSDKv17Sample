#include <Windows.h>
//#include <strsafe.h>
#include <iostream>
#include <NuiApi.h>
#include <KinectInteraction.h>
using namespace std;
#define SafeRelease(X) if(X) delete X;
//----------------------------------------------------
//#define _WINDOWS
INuiSensor            *m_pNuiSensor;

INuiInteractionStream *m_nuiIStream;
class CIneractionClient:public INuiInteractionClient
{
public:
    CIneractionClient()
    {;}
    ~CIneractionClient()
    {;}

    STDMETHOD(GetInteractionInfoAtLocation)(THIS_ DWORD skeletonTrackingId, NUI_HAND_TYPE handType, FLOAT x, FLOAT y, _Out_ NUI_INTERACTION_INFO *pInteractionInfo)
    {        
        if(pInteractionInfo)
        {
            pInteractionInfo->IsPressTarget         = false;
            pInteractionInfo->PressTargetControlId  = 0;
            pInteractionInfo->PressAttractionPointX = 0.f;
            pInteractionInfo->PressAttractionPointY = 0.f;
            pInteractionInfo->IsGripTarget          = true;
            return S_OK;
        }
        return E_POINTER;

        //return S_OK; 

    }

    STDMETHODIMP_(ULONG)    AddRef()                                    { return 2;     }
    STDMETHODIMP_(ULONG)    Release()                                   { return 1;     }
    STDMETHODIMP            QueryInterface(REFIID riid, void **ppv)     { return S_OK;  }

};

CIneractionClient m_nuiIClient;
//--------------------------------------------------------------------
HANDLE m_hNextColorFrameEvent;
HANDLE m_hNextDepthFrameEvent;
HANDLE m_hNextSkeletonEvent;
HANDLE m_hNextInteractionEvent;
HANDLE m_pColorStreamHandle;
HANDLE m_pDepthStreamHandle;
HANDLE m_hEvNuiProcessStop;
//-----------------------------------------------------------------------------------

int DrawColor(HANDLE h)
{
    return 0;
}

int DrawDepth(HANDLE h)
{
    NUI_IMAGE_FRAME pImageFrame;
    INuiFrameTexture* pDepthImagePixelFrame;
    HRESULT hr = m_pNuiSensor->NuiImageStreamGetNextFrame( h, 0, &pImageFrame );
    BOOL nearMode = TRUE;
    m_pNuiSensor->NuiImageFrameGetDepthImagePixelFrameTexture(m_pDepthStreamHandle, &pImageFrame, &nearMode, &pDepthImagePixelFrame);
    INuiFrameTexture * pTexture = pDepthImagePixelFrame;
    NUI_LOCKED_RECT LockedRect;  
    pTexture->LockRect( 0, &LockedRect, NULL, 0 );  
    if( LockedRect.Pitch != 0 )
    {
        HRESULT hr = m_nuiIStream->ProcessDepth(LockedRect.size,PBYTE(LockedRect.pBits),pImageFrame.liTimeStamp);
        if( FAILED( hr ) )
        {
            cout<<"Process Depth failed"<<endl;
        }
    }
    pTexture->UnlockRect(0);
    m_pNuiSensor->NuiImageStreamReleaseFrame( h, &pImageFrame );
    return 0;
}

int DrawSkeleton()
{
    NUI_SKELETON_FRAME SkeletonFrame = {0};
    HRESULT hr = m_pNuiSensor->NuiSkeletonGetNextFrame( 0, &SkeletonFrame );
    if( FAILED( hr ) )
    {
        cout<<"Get Skeleton Image Frame Failed"<<endl;
        return -1;
    }

    bool bFoundSkeleton = true;
    bFoundSkeleton = true;  
    static int static_one_is_enough=0;
    if(static_one_is_enough==0)
    {
        cout<<"find skeleton !"<<endl;
        static_one_is_enough++;
    }

    m_pNuiSensor->NuiTransformSmooth(&SkeletonFrame,NULL); 

    Vector4 v;
    m_pNuiSensor->NuiAccelerometerGetCurrentReading(&v);
    // m_nuiIStream->ProcessSkeleton(i,&SkeletonFrame.SkeletonData[i],&v,SkeletonFrame.liTimeStamp);
    hr =m_nuiIStream->ProcessSkeleton(NUI_SKELETON_COUNT, 
        SkeletonFrame.SkeletonData,
        &v,
        SkeletonFrame.liTimeStamp);
    if( FAILED( hr ) )
    {
        cout<<"Process Skeleton failed"<<endl;
    }

    return 0;
}

int ShowInteraction()
{
    NUI_INTERACTION_FRAME Interaction_Frame;
    auto ret = m_nuiIStream->GetNextFrame( 0,&Interaction_Frame );
    if( FAILED( ret  ) ) {
        cout<<"Failed GetNextFrame"<<endl;
        return 0;
    }

    int trackingID = 0;
    int event=0;

    COORD pos = {0,0};
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleCursorPosition(hOut, pos);   

    cout<<"show Interactions!"<<endl;
    for(int i=0;i<NUI_SKELETON_COUNT;i++)
    {
        trackingID = Interaction_Frame.UserInfos[i].SkeletonTrackingId;

        event=Interaction_Frame.UserInfos[i].HandPointerInfos->HandEventType;

        if ( event == NUI_HAND_EVENT_TYPE_GRIP) {
            cout<<"id="<<trackingID<<"---------event:"<<"Grip"<<endl;
        }
        else if ( event == NUI_HAND_EVENT_TYPE_GRIPRELEASE) {
            cout<<"id="<<trackingID<<"---------event:"<<"GripRelease"<<endl;
        }
        else  {
            cout<<"id="<<trackingID<<"---------event:"<<"None"<<endl;
        }

    }

    return 0;
}

DWORD WINAPI KinectDataThread(LPVOID pParam)
{
    HANDLE hEvents[5] = {m_hEvNuiProcessStop,m_hNextColorFrameEvent,
        m_hNextDepthFrameEvent,m_hNextSkeletonEvent,m_hNextInteractionEvent};

    while(1)
    {
        int nEventIdx;
        nEventIdx=WaitForMultipleObjects(sizeof(hEvents)/sizeof(hEvents[0]),
            hEvents,FALSE,100);
        if (WAIT_OBJECT_0 == WaitForSingleObject(m_hEvNuiProcessStop, 0))
        {
            break;
        }
        // Process signal events
        if (WAIT_OBJECT_0 == WaitForSingleObject(m_hNextColorFrameEvent, 0))
        {
            DrawColor(m_pColorStreamHandle);
        }
        if (WAIT_OBJECT_0 == WaitForSingleObject(m_hNextDepthFrameEvent, 0))
        {
            DrawDepth(m_pDepthStreamHandle);
        }
        if (WAIT_OBJECT_0 == WaitForSingleObject(m_hNextSkeletonEvent, 0))
        {
            DrawSkeleton();
        }
        if (WAIT_OBJECT_0 == WaitForSingleObject(m_hNextInteractionEvent, 0))
        {
            ShowInteraction();
        }
    }

    CloseHandle(m_hEvNuiProcessStop);
    m_hEvNuiProcessStop = NULL;
    CloseHandle( m_hNextSkeletonEvent );
    CloseHandle( m_hNextDepthFrameEvent );
    CloseHandle( m_hNextColorFrameEvent );
    CloseHandle( m_hNextInteractionEvent );
    return 0;
}

DWORD ConnectKinect()
{
    INuiSensor * pNuiSensor;
    HRESULT hr;
    int iSensorCount = 0;
    hr = NuiGetSensorCount(&iSensorCount);
    if (FAILED(hr))
    {
        return hr;
    }
    // Look at each Kinect sensor
    for (int i = 0; i < iSensorCount; ++i)
    {
        // Create the sensor so we can check status, if we can't create it, move on to the next
        hr = NuiCreateSensorByIndex(i, &pNuiSensor);
        if (FAILED(hr))
        {
            continue;
        }
        // Get the status of the sensor, and if connected, then we can initialize it
        hr = pNuiSensor->NuiStatus();
        if (S_OK == hr)
        {
            m_pNuiSensor = pNuiSensor;
            break;
        }
        // This sensor wasn't OK, so release it since we're not using it
        pNuiSensor->Release();
    }
    if (NULL != m_pNuiSensor)
    {
        if (SUCCEEDED(hr))
        {   
            hr = m_pNuiSensor->NuiInitialize(\
                NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX|\
                NUI_INITIALIZE_FLAG_USES_COLOR|\
                NUI_INITIALIZE_FLAG_USES_SKELETON);
            if( hr != S_OK )
            {
                cout<<"NuiInitialize failed"<<endl;
                return hr;
            }

            m_hNextColorFrameEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
            m_pColorStreamHandle = NULL;

            hr = m_pNuiSensor->NuiImageStreamOpen(
                NUI_IMAGE_TYPE_COLOR,NUI_IMAGE_RESOLUTION_640x480, 0, 2, 
                m_hNextColorFrameEvent, &m_pColorStreamHandle);
            if( FAILED( hr ) )
            {
                cout<<"Could not open image stream video"<<endl;
                return hr;
            }

            m_hNextDepthFrameEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
            m_pDepthStreamHandle = NULL;

            hr = m_pNuiSensor->NuiImageStreamOpen( 
                NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX,
                NUI_IMAGE_RESOLUTION_640x480, 0, 2, 
                m_hNextDepthFrameEvent, &m_pDepthStreamHandle);
            if( FAILED( hr ) )
            {
                cout<<"Could not open depth stream video"<<endl;
                return hr;
            }
            m_hNextSkeletonEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
            hr = m_pNuiSensor->NuiSkeletonTrackingEnable( 
                m_hNextSkeletonEvent, 
                NUI_SKELETON_TRACKING_FLAG_ENABLE_IN_NEAR_RANGE//|
                );
            //0);
            if( FAILED( hr ) )
            {
                cout<<"Could not open skeleton stream video"<<endl;
                return hr;
            }
        }
    }
    if (NULL == m_pNuiSensor || FAILED(hr))
    {
        cout<<"No ready Kinect found!"<<endl;
        return E_FAIL;
    }
    return hr;
}

int main()
{
    ConnectKinect();
    HRESULT hr;
    m_hNextInteractionEvent = CreateEvent( NULL,TRUE,FALSE,NULL );
    m_hEvNuiProcessStop = CreateEvent(NULL,TRUE,FALSE,NULL);
    hr = NuiCreateInteractionStream(m_pNuiSensor,(INuiInteractionClient *)&m_nuiIClient,&m_nuiIStream);
    if( FAILED( hr ) )
    {
        cout<<"Could not open Interation stream video"<<endl;
        return hr;
    }
    // hr = NuiCreateInteractionStream(m_pNuiSensor,0,&m_nuiIStream);
    hr = m_nuiIStream->Enable(m_hNextInteractionEvent);
    if( FAILED( hr ) )
    {
        cout<<"Could not open Interation stream video"<<endl;
        return hr;
    }
    HANDLE m_hProcesss = CreateThread(NULL, 0, KinectDataThread, 0, 0, 0);
    while(1)
    {
        Sleep(1);
    }
    m_pNuiSensor->NuiShutdown();
    SafeRelease(m_pNuiSensor);
    return 0;
}

#if 0
// http://social.msdn.microsoft.com/Forums/ja-JP/kinectsdknuiapi/thread/e4f5a696-ed4f-4a5f-8e54-4b3706f62ad0
#include <iostream>

#include <Windows.h>
#include <NuiApi.h>
#include <KinectInteraction.h>


#include <opencv2/opencv.hpp>



#define ERROR_CHECK( ret )  \
    if ( ret != S_OK ) {    \
    std::stringstream ss;	\
    ss << "failed " #ret " " << std::hex << ret << std::endl;			\
    throw std::runtime_error( ss.str().c_str() );			\
    }

const NUI_IMAGE_RESOLUTION CAMERA_RESOLUTION = NUI_IMAGE_RESOLUTION_640x480;

class KinectAdapter : public INuiInteractionClient
{
public:

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv)
    {
        std::cout << __FUNCTION__ << std::endl;
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef()
    {
        std::cout << __FUNCTION__ << std::endl;
        return 2;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        std::cout << __FUNCTION__ << std::endl;
        return 1;
    }

    HRESULT STDMETHODCALLTYPE GetInteractionInfoAtLocation(DWORD skeletonTrackingId, NUI_HAND_TYPE handType, FLOAT x, FLOAT y, _Out_ NUI_INTERACTION_INFO *pInteractionInfo)
    {
        //std::cout << __FUNCTION__ << std::endl;
        pInteractionInfo->IsGripTarget          = true;
        pInteractionInfo->IsPressTarget         = false;
        pInteractionInfo->PressTargetControlId  = 0;
        pInteractionInfo->PressAttractionPointX = 0.0f;
        pInteractionInfo->PressAttractionPointY = 0.0f;
        return S_OK;
    }
};

class KinectSample
{
private:

    INuiSensor* kinect;
    INuiInteractionStream* stream;
    KinectAdapter adapter;

    HANDLE imageStreamHandle;
    HANDLE depthStreamHandle;
    HANDLE streamEvent;

    DWORD width;
    DWORD height;

public:

    KinectSample()
    {
    }

    ~KinectSample()
    {
        // 終了処理
        if ( kinect != 0 ) {
            kinect->NuiShutdown();
            kinect->Release();
        }
    }

    void initialize()
    {
        createInstance();

        // Kinectの設定を初期化する
        ERROR_CHECK( kinect->NuiInitialize( NUI_INITIALIZE_FLAG_USES_COLOR | NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | NUI_INITIALIZE_FLAG_USES_SKELETON ) );

        // RGBカメラを初期化する
        ERROR_CHECK( kinect->NuiImageStreamOpen( NUI_IMAGE_TYPE_COLOR, CAMERA_RESOLUTION,
            0, 2, 0, &imageStreamHandle ) );

        // 距離カメラを初期化する
        ERROR_CHECK( kinect->NuiImageStreamOpen( NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX, CAMERA_RESOLUTION,
            0, 2, 0, &depthStreamHandle ) );

        // Nearモード
        //ERROR_CHECK( kinect->NuiImageStreamSetImageFrameFlags(
        //  depthStreamHandle, NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE ) );

        // スケルトンを初期化する
        ERROR_CHECK( kinect->NuiSkeletonTrackingEnable( 0, NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT ) );

        // フレーム更新イベントのハンドルを作成する
        streamEvent = ::CreateEvent( 0, TRUE, FALSE, 0 );
        ERROR_CHECK( kinect->NuiSetFrameEndEvent( streamEvent, 0 ) );

        // 指定した解像度の、画面サイズを取得する
        ::NuiImageResolutionToSize(CAMERA_RESOLUTION, width, height );

        // インタラクションライブラリの初期化
        ERROR_CHECK( ::NuiCreateInteractionStream( kinect, &adapter, &stream ) );
        ERROR_CHECK( stream->Enable( 0 ) );
    }

    void run()
    {
        cv::Mat image;

        // メインループ
        while ( 1 ) {
            // データの更新を待つ
            DWORD ret = ::WaitForSingleObject( streamEvent, INFINITE );
            ::ResetEvent( streamEvent );

            drawRgbImage( image );
            processDepth();
            processSkeleton();
            processInteraction();

            // 画像を表示する
            cv::imshow( "KinectSample", image );

            // 終了のためのキー入力チェック兼、表示のためのウェイト
            int key = cv::waitKey( 10 );
            if ( key == 'q' ) {
                break;
            }
        }
    }

private:

    void createInstance()
    {
        // 接続されているKinectの数を取得する
        int count = 0;
        ERROR_CHECK( ::NuiGetSensorCount( &count ) );
        if ( count == 0 ) {
            throw std::runtime_error( "Kinect を接続してください" );
        }

        // 最初のKinectのインスタンスを作成する
        ERROR_CHECK( ::NuiCreateSensorByIndex( 0, &kinect ) );

        // Kinectの状態を取得する
        HRESULT status = kinect->NuiStatus();
        if ( status != S_OK ) {
            throw std::runtime_error( "Kinect が利用可能ではありません" );
        }
    }

    void drawRgbImage( cv::Mat& image )
    {
        // RGBカメラのフレームデータを取得する
        NUI_IMAGE_FRAME imageFrame = { 0 };
        ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( imageStreamHandle, 0, &imageFrame ) );

        // 画像データを取得する
        NUI_LOCKED_RECT colorData;
        imageFrame.pFrameTexture->LockRect( 0, &colorData, 0, 0 );

        // 画像データをコピーする
        image = cv::Mat( height, width, CV_8UC4, colorData.pBits );

        // フレームデータを解放する
        ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( imageStreamHandle, &imageFrame ) );
    }

    std::vector<unsigned char> buffer;

    // http://social.msdn.microsoft.com/Forums/en-US/kinectsdknuiapi/thread/e4f5a696-ed4f-4a5f-8e54-4b3706f62ad0
    void processDepth()
    {
        // 距離カメラのフレームデータを取得する
        NUI_IMAGE_FRAME depthFrame = { 0 };
        ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( depthStreamHandle, 0, &depthFrame ) );

        // フレームデータを元に、拡張距離データを取得する
        BOOL nearMode = FALSE;
        INuiFrameTexture *frameTexture = 0;
        ERROR_CHECK( kinect->NuiImageFrameGetDepthImagePixelFrameTexture( depthStreamHandle, &depthFrame, &nearMode, &frameTexture ) );

        // 距離データを取得する
        NUI_LOCKED_RECT depthData = { 0 };
        frameTexture->LockRect( 0, &depthData, 0, 0 );

        // Depthデータを設定する
        buffer.resize( depthData.size );
        if ( depthData.Pitch ) {
            memcpy( &buffer[0], depthData.pBits, buffer.size() );
        }

        ERROR_CHECK( stream->ProcessDepth( buffer.size(), &buffer[0], depthFrame.liTimeStamp ) );

        // フレームデータを解放する
        frameTexture->UnlockRect( 0 );
        ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( depthStreamHandle, &depthFrame ) );
    }

    std::vector<NUI_SKELETON_DATA> skeletons;

    void processSkeleton()
    {
        // スケルトンのフレームを取得する
        NUI_SKELETON_FRAME skeletonFrame = { 0 };
        auto ret = kinect->NuiSkeletonGetNextFrame( 0, &skeletonFrame );
        if ( ret != S_OK ) {
            std::cout << "not skeleton!!" << std::endl;
            return;
        }

        //std::cout << "skeleton!!" << std::endl;

        if ( skeletons.size() != 6 ) {
            skeletons.resize( 6 );
        }

        memcpy( &skeletons[0], skeletonFrame.SkeletonData, sizeof(NUI_SKELETON_DATA) * 6 );

        // スケルトンデータを設定する
        Vector4 reading = { 0 };
        ERROR_CHECK( kinect->NuiAccelerometerGetCurrentReading( &reading ) );
        ERROR_CHECK( stream->ProcessSkeleton( NUI_SKELETON_COUNT, &skeletons[0], &reading, skeletonFrame.liTimeStamp ) );
    }

    void processInteraction()
    {
        // インタラクションフレームを取得する
        NUI_INTERACTION_FRAME interactionFrame = { 0 } ;
        auto ret = stream->GetNextFrame( 0, &interactionFrame );
        if ( ret != S_OK ) {
            if ( ret == E_POINTER ) {
                std::cout << "E_POINTER" << std::endl;
            }
            else if ( ret == E_NUI_FRAME_NO_DATA ) {
                std::cout << "E_NUI_FRAME_NO_DATA" << std::endl;
            }

            return;
        }

        //std::cout << "interaction!!" << std::endl;

        for ( auto user : interactionFrame.UserInfos ) {
            if ( user.SkeletonTrackingId != 0 ) {
                for ( auto hand : user.HandPointerInfos ) {
                    if ( hand.HandEventType != NUI_HAND_EVENT_TYPE::NUI_HAND_EVENT_TYPE_NONE ) {
                        std::cout << EventTypeToString( hand.HandEventType ) << " " << std::endl;
                    }
                }
            }
        }
    }

    std::string EventTypeToString( NUI_HAND_EVENT_TYPE eventType )
    {
        if ( eventType == NUI_HAND_EVENT_TYPE::NUI_HAND_EVENT_TYPE_GRIP ) {
            return "Grip";
        }
        else  if ( eventType == NUI_HAND_EVENT_TYPE::NUI_HAND_EVENT_TYPE_GRIPRELEASE ) {
            return "GripRelease";
        }

        return "None";
    }
};

void main()
{

    try {
        KinectSample kinect;
        kinect.initialize();
        kinect.run();
    }
    catch ( std::exception& ex ) {
        std::cout << ex.what() << std::endl;
    }
}
#endif