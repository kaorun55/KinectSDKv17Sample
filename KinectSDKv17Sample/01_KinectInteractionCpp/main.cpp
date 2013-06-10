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
        return 1;
    }

    ULONG STDMETHODCALLTYPE Release()
    {
        std::cout << __FUNCTION__ << std::endl;
        return 0;
    }

    HRESULT STDMETHODCALLTYPE GetInteractionInfoAtLocation(DWORD skeletonTrackingId, NUI_HAND_TYPE handType, FLOAT x, FLOAT y, _Out_ NUI_INTERACTION_INFO *pInteractionInfo)
    {
        //std::cout << __FUNCTION__ << std::endl;
        pInteractionInfo->IsGripTarget = TRUE;
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

    LARGE_INTEGER skeletonTimeStamp;

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
        skeletonTimeStamp = skeletonFrame.liTimeStamp;
        Vector4 reading = { 0 };
        ERROR_CHECK( kinect->NuiAccelerometerGetCurrentReading( &reading ) );
        ERROR_CHECK( stream->ProcessSkeleton( NUI_SKELETON_COUNT, skeletonFrame.SkeletonData, &reading, skeletonTimeStamp ) );
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
