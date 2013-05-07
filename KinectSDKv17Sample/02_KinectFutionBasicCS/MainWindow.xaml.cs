using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Windows.Threading;
using Microsoft.Kinect;
using Microsoft.Kinect.Toolkit.Fusion;

namespace _02_KinectFutionBasicCS
{
    /// <summary>
    /// MainWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class MainWindow : Window, IDisposable
    {
        KinectSensor kinect;

        #region KinectFusion
        /// <summary>
        /// The Kinect Fusion volume
        /// </summary>
        private Reconstruction volume;

        /// <summary>
        /// Intermediate storage for the depth float data converted from depth image frame
        /// </summary>
        private FusionFloatImageFrame depthFloatBuffer;

        /// <summary>
        /// Intermediate storage for the point cloud data converted from depth float image frame
        /// </summary>
        private FusionPointCloudImageFrame pointCloudBuffer;

        /// <summary>
        /// Raycast shaded surface image
        /// </summary>
        private FusionColorImageFrame shadedSurfaceColorFrame;

        /// <summary>
        /// The reconstruction volume voxel density in voxels per meter (vpm)
        /// 1000mm / 256vpm = ~3.9mm/voxel
        /// </summary>
        private const int VoxelsPerMeter = 256;

        /// <summary>
        /// The reconstruction volume voxel resolution in the X axis
        /// At a setting of 256vpm the volume is 512 / 256 = 2m wide
        /// </summary>
        private const int VoxelResolutionX = 512;

        /// <summary>
        /// The reconstruction volume voxel resolution in the Y axis
        /// At a setting of 256vpm the volume is 384 / 256 = 1.5m high
        /// </summary>
        private const int VoxelResolutionY = 384;

        /// <summary>
        /// The reconstruction volume voxel resolution in the Z axis
        /// At a setting of 256vpm the volume is 512 / 256 = 2m deep
        /// </summary>
        private const int VoxelResolutionZ = 512;

        /// <summary>
        /// The zero-based device index to choose for reconstruction processing if the 
        /// ReconstructionProcessor AMP options are selected.
        /// Here we automatically choose a device to use for processing by passing -1, 
        /// </summary>
        private const int DeviceToUse = -1;

        private const DepthImageFormat depthFormat = DepthImageFormat.Resolution640x480Fps30;
        private const int DepthWidth = 640;
        private const int DepthHeight = 480;
        #endregion

        public MainWindow()
        {
            InitializeComponent();

            Loaded += MainWindow_Loaded;
        }

        /// <summary>
        /// Finalizes an instance of the MainWindow class.
        /// This destructor will run only if the Dispose method does not get called.
        /// </summary>
        ~MainWindow()
        {
            this.Dispose( false );
        }

        /// <summary>
        /// Dispose the allocated frame buffers and reconstruction.
        /// </summary>
        public void Dispose()
        {
            this.Dispose( true );

            // This object will be cleaned up by the Dispose method.
            GC.SuppressFinalize( this );
        }

        /// <summary>
        /// Frees all memory associated with the FusionImageFrame.
        /// </summary>
        /// <param name="disposing">Whether the function was called from Dispose.</param>
        private bool disposed = false;
        protected virtual void Dispose( bool disposing )
        {
            if ( !disposed ) {
                if ( depthFloatBuffer != null ) {
                    depthFloatBuffer.Dispose();
                    depthFloatBuffer = null;
                }

                if ( pointCloudBuffer != null ) {
                    pointCloudBuffer.Dispose();
                    pointCloudBuffer = null;
                }

                if ( shadedSurfaceColorFrame != null ) {
                    shadedSurfaceColorFrame.Dispose();
                    shadedSurfaceColorFrame = null;
                }

                if ( volume != null ) {
                    volume.Dispose();
                    volume = null;
                }

                disposed = true;
            }
        }

        void MainWindow_Loaded( object sender, RoutedEventArgs e )
        {
            try {
                InitializeKinectFusion();
                InitializeKinect();
            }
            catch ( Exception ex ) {
                MessageBox.Show( ex.Message );
            }
        }

        private void InitializeKinect()
        {
            // Kinectの初期化(Depthだけ使う)
            kinect = KinectSensor.KinectSensors[0];
            kinect.DepthStream.Range = DepthRange.Near;
            kinect.DepthStream.Enable( depthFormat );
            kinect.DepthFrameReady += kinect_DepthFrameReady;
            kinect.Start();
        }

        private void InitializeKinectFusion()
        {
            // KinecFusionの初期化
            var volParam = new ReconstructionParameters( VoxelsPerMeter, VoxelResolutionX, VoxelResolutionY, VoxelResolutionZ );
            volume = Reconstruction.FusionCreateReconstruction( volParam, ReconstructionProcessor.Amp, -1, Matrix4.Identity );

            // 変換バッファの作成
            depthFloatBuffer = new FusionFloatImageFrame( DepthWidth, DepthHeight );
            pointCloudBuffer = new FusionPointCloudImageFrame( DepthWidth, DepthHeight );
            shadedSurfaceColorFrame = new FusionColorImageFrame( DepthWidth, DepthHeight );

            // リセット
            volume.ResetReconstruction( Matrix4.Identity );
        }

        // DepthImagePixelを使って処理する
        private bool processingFrame = false;
        void kinect_DepthFrameReady( object sender, DepthImageFrameReadyEventArgs e )
        {
            using ( DepthImageFrame depthFrame = e.OpenDepthImageFrame() ) {
                if ( depthFrame != null && !processingFrame ) {
                    var depthPixels = new DepthImagePixel[depthFrame.PixelDataLength];
                    depthFrame.CopyDepthImagePixelDataTo( depthPixels );

                    Dispatcher.BeginInvoke(
                        DispatcherPriority.Background,
                        (Action<DepthImagePixel[]>)(d => ProcessDepthData( d )),
                        depthPixels );

                    processingFrame = true;
                }
            }
        }

        private int trackingErrorCount;
        private void ProcessDepthData( DepthImagePixel[] depthPixels )
        {
            try {
                // DepthImagePixel から DepthFloatFrame に変換する
                FusionDepthProcessor.DepthToDepthFloatFrame(
                    depthPixels,
                    DepthWidth,
                    DepthHeight,
                    depthFloatBuffer,
                    FusionDepthProcessor.DefaultMinimumDepth,
                    FusionDepthProcessor.DefaultMaximumDepth,
                    false );

                // フレームを処理する
                bool trackingSucceeded = volume.ProcessFrame(
                    depthFloatBuffer,
                    FusionDepthProcessor.DefaultAlignIterationCount,
                    FusionDepthProcessor.DefaultIntegrationWeight,
                    volume.GetCurrentWorldToCameraTransform() );
                if ( !trackingSucceeded ) {
                    // 一定数エラーになったらリセット
                    // Kinectまたは対象を素早く動かしすぎ などの場合
                    trackingErrorCount++;
                    if ( trackingErrorCount >= 100 ) {
                        Trace.WriteLine( @"tracking error." );

                        trackingErrorCount = 0;
                        volume.ResetReconstruction( Matrix4.Identity );
                    }

                    return;
                }

                // PointCloudを取得する
                volume.CalculatePointCloud(
                    pointCloudBuffer,
                    volume.GetCurrentWorldToCameraTransform() );
                    
                // PointCloudを2次元のデータに描画する
                FusionDepthProcessor.ShadePointCloud(
                    pointCloudBuffer,
                    volume.GetCurrentWorldToCameraTransform(),
                    shadedSurfaceColorFrame,
                    null );

                // 2次元のデータをBitmapに書きだす
                var colorPixels = new int[depthPixels.Length];
                shadedSurfaceColorFrame.CopyPixelDataTo( colorPixels );

                ImageKinectFusion.Source = BitmapSource.Create( DepthWidth, DepthHeight, 96, 96,
                    PixelFormats.Bgr32, null, colorPixels, DepthWidth * 4 );

                // カメラ座標のマトリックスをダンプ
                var m = volume.GetCurrentWorldToCameraTransform();
                Trace.WriteLine( string.Format( "{0},{1},{2},{3}", m.M11, m.M12, m.M13, m.M14 ) );
                Trace.WriteLine( string.Format( "{0},{1},{2},{3}", m.M21, m.M22, m.M23, m.M24 ) );
                Trace.WriteLine( string.Format( "{0},{1},{2},{3}", m.M31, m.M32, m.M33, m.M34 ) );
                Trace.WriteLine( string.Format( "{0},{1},{2},{3}", m.M41, m.M42, m.M43, m.M44 ) );
                Trace.WriteLine( string.Format( "" ) );
            }
            catch ( Exception ex ) {
                Trace.WriteLine( ex.Message );
            }
            finally {
                processingFrame = false;
            }
        }
    }
}
