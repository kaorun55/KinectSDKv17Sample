using Microsoft.Kinect;
using Microsoft.Kinect.Toolkit.Interaction;
using System;
using System.Collections.Generic;
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
using System.Globalization;
using System.Diagnostics;

namespace _01_KinectInteractionCS
{
    /// <summary>
    /// MainWindow.xaml の相互作用ロジック
    /// </summary>
    public partial class MainWindow : Window
    {
        KinectSensor kinect;
        InteractionStream stream;

        // 検出するデータを返す
        public class KinectAdapter : IInteractionClient
        {
            public InteractionInfo GetInteractionInfoAtLocation( int skeletonTrackingId, InteractionHandType handType, double x, double y )
            {
                return new InteractionInfo()
                {
                    IsGripTarget = true,
                };

            }
        }

        public MainWindow()
        {
            InitializeComponent();

            Loaded += MainWindow_Loaded;
        }

        void MainWindow_Loaded( object sender, RoutedEventArgs e )
        {
            // Kinectの初期化
            kinect = KinectSensor.KinectSensors[0];
            kinect.AllFramesReady += kinect_AllFramesReady;
            kinect.ColorStream.Enable();
            kinect.DepthStream.Enable();
            kinect.SkeletonStream.Enable();
            kinect.Start();

            // インタラクションライブラリの初期化
            stream = new InteractionStream( kinect, new KinectAdapter() );
            stream.InteractionFrameReady += stream_InteractionFrameReady;
        }

        void kinect_AllFramesReady( object sender, AllFramesReadyEventArgs e )
        {
            using ( var colorFrame = e.OpenColorImageFrame() ) {
                if ( colorFrame != null ) {
                    var pixel = new byte[colorFrame.PixelDataLength];
                    colorFrame.CopyPixelDataTo( pixel );

                    ImageRgb.Source = BitmapSource.Create( colorFrame.Width, colorFrame.Height, 96, 96,
                        PixelFormats.Bgr32, null, pixel, colorFrame.Width * 4 ); 
                }
            }

            using ( var depthFrame = e.OpenDepthImageFrame() ) {
                if ( depthFrame != null ) {
                    // Depth情報を入れる
                    // GetRawPixelData()はインタラクションライブラリ内で実装された拡張メソッド
                    stream.ProcessDepth( depthFrame.GetRawPixelData(), depthFrame.Timestamp );
                }
            }

            using ( var skeletonFrame = e.OpenSkeletonFrame() ) {
                if ( skeletonFrame != null ) {
                    var skeletons = new Skeleton[skeletonFrame.SkeletonArrayLength];
                    skeletonFrame.CopySkeletonDataTo( skeletons );

                    // スケルトン情報を入れる
                    stream.ProcessSkeleton( skeletons, kinect.AccelerometerGetCurrentReading(), skeletonFrame.Timestamp );
                }
            }
        }

        void stream_InteractionFrameReady( object sender, InteractionFrameReadyEventArgs e )
        {
            using ( var interactionFrame = e.OpenInteractionFrame() ) {
                if ( interactionFrame != null ) {
                    var userInfos = new UserInfo[InteractionFrame.UserInfoArrayLength];
                    interactionFrame.CopyInteractionDataTo( userInfos );

                    List<InteractionHandPointer> hands = new List<InteractionHandPointer>();

                    foreach ( var user in userInfos ) {
                        if ( user.SkeletonTrackingId != 0 ) {
                            foreach ( var hand in user.HandPointers ) {
                                hands.Add( hand );
                            }
                        }
                    }

                    Grid.ItemsSource = hands;
                }
            }

        }

        private void Grid_AutoGeneratingColumn( object sender, DataGridAutoGeneratingColumnEventArgs e )
        {
            if ( e.PropertyType == typeof( double ) ) {
                DataGridTextColumn dataGridTextColumn = e.Column as DataGridTextColumn;
                if ( dataGridTextColumn != null ) {
                    dataGridTextColumn.Binding.StringFormat = "{0:f3}";
                }
            }
        }
    }
}
