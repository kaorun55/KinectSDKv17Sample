using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
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

namespace _03_KinectFusionBasicCS
{
    /// <summary>
    /// MainWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class MainWindow : Window
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

        private bool processingFrame = false;
        private bool processingSaveFile = false;

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
                if ( processingSaveFile ) {
                    return;
                }

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
            }
            catch ( Exception ex ) {
                Trace.WriteLine( ex.Message );
            }
            finally {
                processingFrame = false;
            }
        }

        private void Button_Click_1( object sender, RoutedEventArgs e )
        {
            ProcessSaveFile();
        }

        private void ProcessSaveFile()
        {
            try {
                processingSaveFile = true;

                var mesh = this.volume.CalculateMesh( 1 );

                using ( BinaryWriter writer = new BinaryWriter( File.OpenWrite( @"mesh.stl" ) ) ) {
                    SaveBinarySTLMesh( mesh, writer );
                }

                using ( StreamWriter writer = new StreamWriter( @"mesh.obj" ) ) {
                    SaveAsciiObjMesh( mesh, writer );
                }
            }
            catch ( Exception ex ) {
                MessageBox.Show( ex.Message );
            }
            finally {
                processingSaveFile = false;
            }
        }

        #region メッシュデータをファイルへ出力(Kinect SDKのサンプルより)
        /// <summary>
        /// Save mesh in binary .STL file
        /// </summary>
        /// <param name="mesh">Calculated mesh object</param>
        /// <param name="writer">Binary file writer</param>
        /// <param name="flipYZ">Flag to determine whether the Y and Z values are flipped on save</param>
        private static void SaveBinarySTLMesh( Mesh mesh, BinaryWriter writer, bool flipYZ = true )
        {
            var vertices = mesh.GetVertices();
            var normals = mesh.GetNormals();
            var indices = mesh.GetTriangleIndexes();

            // Check mesh arguments
            if ( 0 == vertices.Count || 0 != vertices.Count % 3 || vertices.Count != indices.Count ) {
                throw new ArgumentException( "不正なメッシュです" );
            }

            char[] header = new char[80];
            writer.Write( header );

            // Write number of triangles
            int triangles = vertices.Count / 3;
            writer.Write( triangles );

            // Sequentially write the normal, 3 vertices of the triangle and attribute, for each triangle
            for ( int i = 0; i < triangles; i++ ) {
                // Write normal
                var normal = normals[i * 3];
                writer.Write( normal.X );
                writer.Write( flipYZ ? -normal.Y : normal.Y );
                writer.Write( flipYZ ? -normal.Z : normal.Z );

                // Write vertices
                for ( int j = 0; j < 3; j++ ) {
                    var vertex = vertices[(i * 3) + j];
                    writer.Write( vertex.X );
                    writer.Write( flipYZ ? -vertex.Y : vertex.Y );
                    writer.Write( flipYZ ? -vertex.Z : vertex.Z );
                }

                ushort attribute = 0;
                writer.Write( attribute );
            }
        }

        /// <summary>
        /// Save mesh in ASCII Wavefront .OBJ file
        /// </summary>
        /// <param name="mesh">Calculated mesh object</param>
        /// <param name="writer">Stream writer</param>
        /// <param name="flipYZ">Flag to determine whether the Y and Z values are flipped on save</param>
        private static void SaveAsciiObjMesh( Mesh mesh, StreamWriter writer, bool flipYZ = true )
        {
            var vertices = mesh.GetVertices();
            var normals = mesh.GetNormals();
            var indices = mesh.GetTriangleIndexes();

            // Check mesh arguments
            if ( 0 == vertices.Count || 0 != vertices.Count % 3 || vertices.Count != indices.Count ) {
                throw new ArgumentException( "不正なメッシュです" );
            }

            // Write the header lines
            writer.WriteLine( "#" );
            writer.WriteLine( "# OBJ file created by Microsoft Kinect Fusion" );
            writer.WriteLine( "#" );

            // Sequentially write the 3 vertices of the triangle, for each triangle
            for ( int i = 0; i < vertices.Count; i++ ) {
                var vertex = vertices[i];

                string vertexString = "v " + vertex.X.ToString( CultureInfo.CurrentCulture ) + " ";

                if ( flipYZ ) {
                    vertexString += (-vertex.Y).ToString( CultureInfo.CurrentCulture ) + " " + (-vertex.Z).ToString( CultureInfo.CurrentCulture );
                }
                else {
                    vertexString += vertex.Y.ToString( CultureInfo.CurrentCulture ) + " " + vertex.Z.ToString( CultureInfo.CurrentCulture );
                }

                writer.WriteLine( vertexString );
            }

            // Sequentially write the 3 normals of the triangle, for each triangle
            for ( int i = 0; i < normals.Count; i++ ) {
                var normal = normals[i];

                string normalString = "vn " + normal.X.ToString( CultureInfo.CurrentCulture ) + " ";

                if ( flipYZ ) {
                    normalString += (-normal.Y).ToString( CultureInfo.CurrentCulture ) + " " + (-normal.Z).ToString( CultureInfo.CurrentCulture );
                }
                else {
                    normalString += normal.Y.ToString( CultureInfo.CurrentCulture ) + " " + normal.Z.ToString( CultureInfo.CurrentCulture );
                }

                writer.WriteLine( normalString );
            }

            // Sequentially write the 3 vertex indices of the triangle face, for each triangle
            // Note this is typically 1-indexed in an OBJ file when using absolute referencing!
            for ( int i = 0; i < vertices.Count / 3; i++ ) {
                string baseIndex0 = ((i * 3) + 1).ToString( CultureInfo.CurrentCulture );
                string baseIndex1 = ((i * 3) + 2).ToString( CultureInfo.CurrentCulture );
                string baseIndex2 = ((i * 3) + 3).ToString( CultureInfo.CurrentCulture );

                string faceString = "f " + baseIndex0 + "//" + baseIndex0 + " " + baseIndex1 + "//" + baseIndex1 + " " + baseIndex2 + "//" + baseIndex2;
                writer.WriteLine( faceString );
            }
        }
        #endregion
    }
}
