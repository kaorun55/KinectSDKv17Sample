#include <iostream>

#include <Windows.h>
#include <NuiApi.h>
#include <NuiKinectFusionApi.h>

#include <opencv2/opencv.hpp>



#define ERROR_CHECK( ret )  \
    if ( ret != S_OK ) {    \
    std::stringstream ss;	\
    ss << "failed " #ret " " << std::hex << ret << std::endl;			\
    throw std::runtime_error( ss.str().c_str() );			\
    }

const NUI_IMAGE_RESOLUTION CAMERA_RESOLUTION = NUI_IMAGE_RESOLUTION_640x480;

class KinectSample
{
private:

    INuiSensor* kinect;

    INuiFusionReconstruction*   m_pVolume;

    NUI_FUSION_IMAGE_FRAME*     m_pDepthFloatImage;
    NUI_FUSION_IMAGE_FRAME*     m_pPointCloud;
    NUI_FUSION_IMAGE_FRAME*     m_pShadedSurface;

    HANDLE imageStreamHandle;
    HANDLE depthStreamHandle;
    HANDLE streamEvent;

    DWORD width;
    DWORD height;

public:

    KinectSample()
        : kinect( 0 )
        , m_pVolume( 0 )
        , m_pDepthFloatImage( 0 )
        , m_pPointCloud( 0 )
        , m_pShadedSurface( 0 )
        , trackingErrorCount( 0 )
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
        ERROR_CHECK( kinect->NuiImageStreamSetImageFrameFlags(
          depthStreamHandle, NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE ) );

        // スケルトンを初期化する
        ERROR_CHECK( kinect->NuiSkeletonTrackingEnable( 0, NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT ) );

        // フレーム更新イベントのハンドルを作成する
        streamEvent = ::CreateEvent( 0, TRUE, FALSE, 0 );
        ERROR_CHECK( kinect->NuiSetFrameEndEvent( streamEvent, 0 ) );

        // 指定した解像度の、画面サイズを取得する
        ::NuiImageResolutionToSize(CAMERA_RESOLUTION, width, height );

        // KinectFusionの初期化
        initializeKinectFusion();
    }

    void initializeKinectFusion()
    {
        HRESULT hr = S_OK;

        NUI_FUSION_RECONSTRUCTION_PARAMETERS reconstructionParams;
        reconstructionParams.voxelsPerMeter = 256;// 1000mm / 256vpm = ~3.9mm/voxel    
        reconstructionParams.voxelCountX = 512;   // 512 / 256vpm = 2m wide reconstruction
        reconstructionParams.voxelCountY = 384;   // Memory = 512*384*512 * 4bytes per voxel
        reconstructionParams.voxelCountZ = 512;   // This will require a GPU with at least 512MB

        // Reconstruction Volume のインスタンスを生成
        hr = ::NuiFusionCreateReconstruction(
            &reconstructionParams,
            NUI_FUSION_RECONSTRUCTION_PROCESSOR_TYPE_AMP, // NUI_FUSION_RECONSTRUCTION_PROCESSOR_TYPE_CPU
            -1, &IdentityMatrix(), &m_pVolume);
        if (FAILED(hr)){
            throw std::runtime_error( "::NuiFusionCreateReconstruction failed." );
        }

        // DepthFloatImage のインスタンスを生成
        hr = ::NuiFusionCreateImageFrame(NUI_FUSION_IMAGE_TYPE_FLOAT, width, height, nullptr, &m_pDepthFloatImage);
        if (FAILED(hr)) {
            throw std::runtime_error( "::NuiFusionCreateImageFrame failed(Float)." );
        }

        // PointCloud のインスタンスを生成
        hr = ::NuiFusionCreateImageFrame(NUI_FUSION_IMAGE_TYPE_POINT_CLOUD, width, height, nullptr, &m_pPointCloud);
        if (FAILED(hr)) {
            throw std::runtime_error( "::NuiFusionCreateImageFrame failed(PointCloud)." );
        }

        // シェーダーサーフェースのインスタンスを生成
        hr = ::NuiFusionCreateImageFrame(NUI_FUSION_IMAGE_TYPE_COLOR, width, height, nullptr, &m_pShadedSurface);
        if (FAILED(hr)) {
            throw std::runtime_error( "::NuiFusionCreateImageFrame failed(Color)." );
        }

        // リセット
        m_pVolume->ResetReconstruction( &IdentityMatrix(), nullptr );
    }

    /// <summary>
    /// Set Identity in a Matrix4
    /// </summary>
    /// <param name="mat">The matrix to set to identity</param>
    const Matrix4& IdentityMatrix()
    {
        static Matrix4 mat;
        mat.M11 = 1; mat.M12 = 0; mat.M13 = 0; mat.M14 = 0;
        mat.M21 = 0; mat.M22 = 1; mat.M23 = 0; mat.M24 = 0;
        mat.M31 = 0; mat.M32 = 0; mat.M33 = 1; mat.M34 = 0;
        mat.M41 = 0; mat.M42 = 0; mat.M43 = 0; mat.M44 = 1;
        return mat;
    }

    void run()
    {
        cv::Mat image;

        // メインループ
        while ( 1 ) {
            // データの更新を待つ
            DWORD ret = ::WaitForSingleObject( streamEvent, INFINITE );
            ::ResetEvent( streamEvent );

            processDepth( image );

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
        ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( imageStreamHandle, INFINITE, &imageFrame ) );

        // 画像データを取得する
        NUI_LOCKED_RECT colorData;
        imageFrame.pFrameTexture->LockRect( 0, &colorData, 0, 0 );

        // 画像データをコピーする
        image = cv::Mat( height, width, CV_8UC4, colorData.pBits );

        // フレームデータを解放する
        ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( imageStreamHandle, &imageFrame ) );
    }

    void processDepth( cv::Mat& mat )
    {
        // 距離カメラのフレームデータを取得する
        NUI_IMAGE_FRAME depthFrame = { 0 };
        ERROR_CHECK( kinect->NuiImageStreamGetNextFrame( depthStreamHandle, 0, &depthFrame ) );

        // フレームデータを元に、拡張距離データを取得する
		BOOL nearMode = FALSE;
		INuiFrameTexture *frameTexture = 0;
		kinect->NuiImageFrameGetDepthImagePixelFrameTexture( depthStreamHandle, &depthFrame, &nearMode, &frameTexture );

        // 距離データを取得する
        NUI_LOCKED_RECT depthData = { 0 };
        frameTexture->LockRect( 0, &depthData, 0, 0 );
        if ( depthData.Pitch == 0 ) {
            std::cout << "zero" << std::endl;
        }

        // KinectFusionの処理を行う
        processKinectFusion( (NUI_DEPTH_IMAGE_PIXEL*)depthData.pBits, depthData.size, mat );

        // フレームデータを解放する
        ERROR_CHECK( kinect->NuiImageStreamReleaseFrame( depthStreamHandle, &depthFrame ) );
    }

    int trackingErrorCount;
    void processKinectFusion( const NUI_DEPTH_IMAGE_PIXEL* depthPixel, int depthPixelSize, cv::Mat& mat ) 
    {
        // DepthImagePixel から DepthFloaatFrame に変換する
        HRESULT hr = ::NuiFusionDepthToDepthFloatFrame( depthPixel, width, height, m_pDepthFloatImage,
                            NUI_FUSION_DEFAULT_MINIMUM_DEPTH, NUI_FUSION_DEFAULT_MAXIMUM_DEPTH, TRUE );
        if (FAILED(hr)) {
            throw std::runtime_error( "::NuiFusionDepthToDepthFloatFrame failed." );
        }

        // フレームを処理する
        Matrix4 worldToCameraTransform;
        m_pVolume->GetCurrentWorldToCameraTransform( &worldToCameraTransform );
        hr = m_pVolume->ProcessFrame( m_pDepthFloatImage, NUI_FUSION_DEFAULT_ALIGN_ITERATION_COUNT,
                                        NUI_FUSION_DEFAULT_INTEGRATION_WEIGHT, &worldToCameraTransform );
        if (FAILED(hr)) {
            // 一定数エラーになったらリセット
            // Kinectまたは対象を素早く動かしすぎ などの場合
            ++trackingErrorCount;
            if ( trackingErrorCount >= 100 ) {
                trackingErrorCount = 0;
                m_pVolume->ResetReconstruction( &IdentityMatrix(), nullptr );
            }

            return;
        }

        // PointCloudを取得する
        hr = m_pVolume->CalculatePointCloud( m_pPointCloud, &worldToCameraTransform );
        if (FAILED(hr)) {
            throw std::runtime_error( "CalculatePointCloud failed." );
        }

        // PointCloudを2次元のデータに描画する
        hr = ::NuiFusionShadePointCloud( m_pPointCloud, &worldToCameraTransform,
                                    nullptr, m_pShadedSurface, nullptr );
        if (FAILED(hr)) {
            throw std::runtime_error( "::NuiFusionShadePointCloud failed." );
        }

        // 2次元のデータをBitmapに書きだす
        INuiFrameTexture * pShadedImageTexture = m_pShadedSurface->pFrameTexture;
        NUI_LOCKED_RECT ShadedLockedRect;
        hr = pShadedImageTexture->LockRect(0, &ShadedLockedRect, nullptr, 0);
        if (FAILED(hr)) {
            throw std::runtime_error( "LockRect failed." );
        }

        mat = cv::Mat( height, width, CV_8UC4, ShadedLockedRect.pBits );

        // We're done with the texture so unlock it
        pShadedImageTexture->UnlockRect(0);
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
