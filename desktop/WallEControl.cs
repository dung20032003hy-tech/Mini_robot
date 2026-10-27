using System;
using System.Collections.Generic;
using System.Collections;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using System.Xml;

namespace WallE {
    public interface IMotorPointerCapture {
        bool TryCapture(Button button);
        void Release(Button button);
        void ReleaseAll();
    }
    public sealed class WpfMotorPointerCapture : IMotorPointerCapture {
        private readonly Window owner;
        public WpfMotorPointerCapture(Window window) { owner=window; }
        public bool TryCapture(Button button) { return button.CaptureMouse(); }
        public void Release(Button button) { button.ReleaseMouseCapture(); }
        public void ReleaseAll() {
            var captured=Mouse.Captured as DependencyObject;
            if (captured!=null && owner.IsAncestorOf(captured)) Mouse.Capture(null);
        }
    }
    public sealed class UploadJob {
        private Process process;
        private readonly ConcurrentQueue<string> output = new ConcurrentQueue<string>();
        public bool Running { get { return process!=null && !process.HasExited; } }
        public int? ExitCode { get { return process!=null && process.HasExited ? (int?)process.ExitCode : null; } }
        public bool TryRead(out string line) { return output.TryDequeue(out line); }
        public void Start(string project,string port) {
            if (Running) throw new InvalidOperationException("Tác vụ nạp đang chạy.");
            if (!System.Text.RegularExpressions.Regex.IsMatch(port ?? "",@"^COM[1-9][0-9]{0,3}$",
                    System.Text.RegularExpressions.RegexOptions.IgnoreCase))
                throw new ArgumentException("Chọn cổng COM hợp lệ trước khi nạp.");
            string pio=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                                    @".platformio\penv\Scripts\platformio.exe");
            if (!File.Exists(pio)) throw new FileNotFoundException("Cần cài PlatformIO trước khi nạp firmware.",pio);
            process=new Process();
            process.StartInfo=new ProcessStartInfo {
                FileName=pio, Arguments="run -e esp32-s3-n16r8 -t upload --upload-port " + port,
                WorkingDirectory=project, UseShellExecute=false, CreateNoWindow=true,
                RedirectStandardOutput=true, RedirectStandardError=true
            };
            DataReceivedEventHandler receive=delegate(object sender,DataReceivedEventArgs e) {
                if (e.Data!=null && output.Count<300) output.Enqueue(e.Data);
            };
            process.OutputDataReceived+=receive; process.ErrorDataReceived+=receive;
            process.Start(); process.BeginOutputReadLine(); process.BeginErrorReadLine();
        }
    }

    public sealed class ControlWindow {
        public readonly Window Window;
        public readonly RobotController Robot;
        private readonly Dictionary<string,FrameworkElement> elements=new Dictionary<string,FrameworkElement>();
        private readonly Dictionary<Key,int> heldKeys=new Dictionary<Key,int>();
        private readonly List<MotorStep> motorSteps=new List<MotorStep>();
        private readonly List<string> events=new List<string>();
        private readonly DispatcherTimer timer;
        private readonly UploadJob uploader=new UploadJob();
        private readonly IMotorPointerCapture motorPointer;
        private readonly string projectRoot;
        private readonly Stopwatch clock=Stopwatch.StartNew();
        private string page="Drive", expression="normal";
        private bool loadingSettings, armsDirty, eyesDirty, optionsDirty, uploaded;
        private double leftHome=90,rightHome=90;
        private long nextArms, nextEyes;
        private Dictionary<string,object> lastSettings;
        private Dictionary<string,object> lastAiSettings;
        private bool mouseMotion, armLimitsLoaded;
        private bool loadingMotors;

        private T E<T>(string name) where T:FrameworkElement {
            FrameworkElement value;
            if (!elements.TryGetValue(name,out value)) {
                value=Window.FindName(name) as FrameworkElement;
                if (value==null) throw new InvalidOperationException("Missing UI element " + name);
                elements[name]=value;
            }
            return (T)value;
        }
        private void Text(string name,string value) { E<TextBlock>(name).Text=value; }
        private bool Check(string name) { return E<CheckBox>(name).IsChecked==true; }
        private void Click(string name,Action action) {
            E<Button>(name).Click+=delegate {
                try { action(); } catch (Exception ex) { Inform(ex.Message); }
            };
        }
        private void Inform(string text) {
            Text("Feedback",text);
            events.Add(DateTime.Now.ToString("HH:mm:ss") + "  " + text);
            while (events.Count>70) events.RemoveAt(0);
            E<TextBox>("EventLog").Text=String.Join(Environment.NewLine,events);
            E<TextBox>("EventLog").ScrollToEnd();
        }
        public ControlWindow(bool preview, IRobotWire testWire = null, Func<long> testClock = null,
                             IMotorPointerCapture testPointer = null) {
            projectRoot=AppDomain.CurrentDomain.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
            if (!File.Exists(Path.Combine(projectRoot,"platformio.ini")))
                projectRoot=Directory.GetParent(projectRoot).FullName;
            using (Stream resource=Assembly.GetExecutingAssembly().GetManifestResourceStream("WallE.Window.xaml"))
                Window=(Window)XamlReader.Load(resource);
            motorPointer=testPointer ?? new WpfMotorPointerCapture(Window);
            Window.Width=Math.Min(1280,SystemParameters.WorkArea.Width-30);
            Window.Height=Math.Min(850,SystemParameters.WorkArea.Height-30);
            Robot=new RobotController(testWire ?? new SerialWire(),testClock ?? delegate { return clock.ElapsedMilliseconds; });
            Robot.Notice+=Inform;
            Robot.Changed+=Update;
            BuildMotorControls();
            WireEvents();
            RefreshPorts();
            Update();
            timer=new DispatcherTimer { Interval=TimeSpan.FromMilliseconds(40) };
            timer.Tick+=delegate {
                try { Robot.Tick(); FlushControls(); PollUpload(); UpdateMotorControls(); }
                catch (Exception ex) { Robot.Disconnect(); Inform("Đã ngắt để dừng điều khiển: " + ex.Message); }
            };
            if (!preview) timer.Start();
            Window.Deactivated+=delegate { PauseControl(); };
            Window.Closing+=delegate(object sender,System.ComponentModel.CancelEventArgs e) {
                if (uploader.Running) {
                    e.Cancel=true; Inform("Firmware đang nạp. Vui lòng chờ hoàn tất trước khi đóng ứng dụng.");
                    return;
                }
                timer.Stop(); Robot.Dispose();
            };
        }
        private T RegisterMotorElement<T>(T element,string name) where T:FrameworkElement {
            element.Name=name; Window.RegisterName(name,element); elements[name]=element; return element;
        }
        private void BuildMotorControls() {
            for (int i=0;i<4;i++) {
                string id="Motor"+(i+1);
                var panel=new StackPanel();
                panel.Children.Add(new TextBlock { Text="M"+(i+1),FontSize=24,FontWeight=FontWeights.Bold });
                panel.Children.Add(new TextBlock {
                    Text=String.Format("GPIO {0} / {1} · DRV8833",4+i*2,5+i*2),
                    Foreground=(Brush)new BrushConverter().ConvertFromString("#9DAFC5"),FontSize=11,
                    Margin=new Thickness(0,3,0,12)
                });
                panel.Children.Add(RegisterMotorElement(new TextBlock {Text="Mức công suất: 40%",FontSize=12},id+"LevelLabel"));
                panel.Children.Add(RegisterMotorElement(new Slider {
                    Minimum=0,Maximum=100,Value=40,TickFrequency=5,IsSnapToTickEnabled=true,
                    Margin=new Thickness(0,10,0,12)
                },id+"Level"));
                var buttons=new UniformGrid { Columns=2 };
                buttons.Children.Add(RegisterMotorElement(new Button {Content="↻ Thuận",FontSize=12,Padding=new Thickness(6,10,6,10),Margin=new Thickness(0,0,6,10)},id+"Forward"));
                buttons.Children.Add(RegisterMotorElement(new Button {Content="↺ Nghịch",FontSize=12,Padding=new Thickness(6,10,6,10),Margin=new Thickness(0,0,0,10)},id+"Reverse"));
                panel.Children.Add(buttons);
                panel.Children.Add(new TextBlock {Text="Chiều trong tổ hợp",FontSize=11,Margin=new Thickness(0,1,0,5)});
                panel.Children.Add(RegisterMotorElement(new ComboBox {
                    ItemsSource=new[]{"Tắt","Thuận (+)","Nghịch (−)"},SelectedIndex=0,
                    Foreground=Brushes.Black,FontSize=12,Padding=new Thickness(5),MinHeight=30
                },id+"Direction"));
                E<UniformGrid>("MotorChannels").Children.Add(new Border {
                    Style=(Style)Window.FindResource("Card"),Padding=new Thickness(14),Child=panel
                });
            }
        }
        private double MotorLevel(int index) { return E<Slider>("Motor"+(index+1)+"Level").Value/100; }
        private double[] SelectedMotorValues() {
            double[] values=new double[4];
            for (int i=0;i<4;i++) {
                int choice=E<ComboBox>("Motor"+(i+1)+"Direction").SelectedIndex;
                values[i]=(choice==1?1:choice==2?-1:0)*MotorLevel(i);
            }
            return values;
        }
        private static string MotorValuesText(double[] values) {
            return String.Join("   ",values.Select((v,i)=>String.Format("M{0} {1:+0;-0;0}%",i+1,v*100)));
        }
        private void CancelMotorInput() {
            mouseMotion=false; heldKeys.Clear();
            motorPointer.ReleaseAll();
            Robot.ReleaseMotion();
        }
        private void MotorConfigurationChanged() {
            if (loadingMotors) return;
            if (mouseMotion || heldKeys.Count>0 || Robot.SequenceRunning) CancelMotorInput();
            UpdateMotorControls();
        }
        private void PresetMotors(params int[] directions) {
            CancelMotorInput(); loadingMotors=true;
            try {
                for (int i=0;i<4;i++) E<ComboBox>("Motor"+(i+1)+"Direction").SelectedIndex=directions[i]>0?1:directions[i]<0?2:0;
            } finally { loadingMotors=false; }
            UpdateMotorControls();
        }
        private void WireMotorControls() {
            Click("JumpCombination",delegate { E<Border>("MotorCombinationSection").BringIntoView(); });
            Click("JumpSequence",delegate { E<Border>("MotorSequenceSection").BringIntoView(); });
            for (int i=0;i<4;i++) {
                int channel=i;
                string id="Motor"+(i+1);
                MotorHold(id+"Forward",delegate { var v=new double[4]; v[channel]=MotorLevel(channel); return v; });
                MotorHold(id+"Reverse",delegate { var v=new double[4]; v[channel]=-MotorLevel(channel); return v; });
                E<Slider>(id+"Level").ValueChanged+=delegate { MotorConfigurationChanged(); };
                E<ComboBox>(id+"Direction").SelectionChanged+=delegate { MotorConfigurationChanged(); };
            }
            MotorHold("RunCombination",SelectedMotorValues);
            Click("PresetAll",delegate { PresetMotors(1,1,1,1); });
            Click("PresetReverse",delegate { PresetMotors(-1,-1,-1,-1); });
            Click("Preset12",delegate { PresetMotors(1,1,0,0); });
            Click("Preset34",delegate { PresetMotors(0,0,1,1); });
            Click("Preset13",delegate { PresetMotors(1,0,1,0); });
            Click("Preset24",delegate { PresetMotors(0,1,0,1); });
            Click("Preset14",delegate { PresetMotors(1,0,0,1); });
            Click("Preset23",delegate { PresetMotors(0,1,1,0); });
            Click("PresetNone",delegate { PresetMotors(0,0,0,0); });
            Click("PresetInvert",delegate {
                int[] directions=new int[4];
                for (int i=0;i<4;i++) { int choice=E<ComboBox>("Motor"+(i+1)+"Direction").SelectedIndex; directions[i]=choice==1?-1:choice==2?1:0; }
                PresetMotors(directions);
            });
            Click("AddMotorStep",delegate {
                if (Robot.SequenceRunning) return;
                double seconds;
                if (!Double.TryParse(E<TextBox>("StepDuration").Text.Replace(',','.'),NumberStyles.Float,CultureInfo.InvariantCulture,out seconds) ||
                    Double.IsNaN(seconds) || Double.IsInfinity(seconds) || seconds<0.1 || seconds>30)
                    throw new InvalidOperationException("Thời gian mỗi bước phải từ 0,1 đến 30 giây.");
                if (motorSteps.Count>=20) throw new InvalidOperationException("Chuỗi tối đa 20 bước.");
                motorSteps.Add(new MotorStep(SelectedMotorValues(),(int)Math.Round(seconds*1000)));
                RefreshMotorSteps(motorSteps.Count-1);
            });
            Click("RemoveMotorStep",delegate {
                int index=E<ListBox>("MotorSteps").SelectedIndex;
                if (!Robot.SequenceRunning && index>=0) { motorSteps.RemoveAt(index); RefreshMotorSteps(Math.Min(index,motorSteps.Count-1)); }
            });
            Click("ClearMotorSteps",delegate { if (!Robot.SequenceRunning) { motorSteps.Clear(); RefreshMotorSteps(-1); } });
            Click("MoveStepUp",delegate { MoveMotorStep(-1); });
            Click("MoveStepDown",delegate { MoveMotorStep(1); });
            Click("RunMotorSequence",delegate { CancelMotorInput(); Robot.StartSequence(motorSteps); UpdateMotorControls(); });
            Click("StopMotorSequence",delegate { CancelMotorInput(); UpdateMotorControls(); });
            E<ListBox>("MotorSteps").SelectionChanged+=delegate { UpdateMotorControls(); };
        }
        private void MoveMotorStep(int offset) {
            int index=E<ListBox>("MotorSteps").SelectedIndex, destination=index+offset;
            if (Robot.SequenceRunning || index<0 || destination<0 || destination>=motorSteps.Count) return;
            var step=motorSteps[index]; motorSteps.RemoveAt(index); motorSteps.Insert(destination,step);
            RefreshMotorSteps(destination);
        }
        private void RefreshMotorSteps(int selected) {
            E<ListBox>("MotorSteps").ItemsSource=motorSteps.Select((step,i)=>
                String.Format(CultureInfo.InvariantCulture,"{0:00}. {1:0.###} s   {2}",i+1,step.DurationMs/1000.0,MotorValuesText(step.Values))).ToArray();
            E<ListBox>("MotorSteps").SelectedIndex=selected;
            UpdateMotorControls();
        }
        private void UpdateMotorControls() {
            bool running=Robot.SequenceRunning, canRun=Robot.Ready&&Robot.Owned&&Robot.Armed;
            double[] values=SelectedMotorValues();
            for (int i=0;i<4;i++) {
                string id="Motor"+(i+1);
                E<Button>(id+"Forward").IsEnabled=canRun&&!running;
                E<Button>(id+"Reverse").IsEnabled=canRun&&!running;
                E<Slider>(id+"Level").IsEnabled=!running;
                E<ComboBox>(id+"Direction").IsEnabled=!running;
                Text(id+"LevelLabel",String.Format("Mức công suất: {0:0}%",MotorLevel(i)*100));
            }
            foreach (string id in new[]{"PresetAll","PresetReverse","Preset12","Preset34","Preset13","Preset24","Preset14","Preset23","PresetInvert","PresetNone","AddMotorStep","ClearMotorSteps"})
                E<Button>(id).IsEnabled=!running;
            int selected=E<ListBox>("MotorSteps").SelectedIndex;
            E<Button>("MoveStepUp").IsEnabled=!running&&selected>0;
            E<Button>("MoveStepDown").IsEnabled=!running&&selected>=0&&selected<motorSteps.Count-1;
            E<Button>("RemoveMotorStep").IsEnabled=!running&&selected>=0;
            E<TextBox>("StepDuration").IsEnabled=!running;
            E<Button>("RunCombination").IsEnabled=canRun&&!running&&values.Any(v=>v!=0);
            E<Button>("RunMotorSequence").IsEnabled=canRun&&!running&&motorSteps.Count>0;
            E<Button>("StopMotorSequence").IsEnabled=running;
            Text("MotorCombination",values.Any(v=>v!=0)?MotorValuesText(values):"Chưa chọn motor trong tổ hợp. Chọn chiều quay ở từng ô motor.");
            Text("MotorSequenceStatus",running?
                String.Format("Đang chạy bước {0}/{1} · còn {2:0.0} s",Robot.SequenceIndex+1,Robot.SequenceCount,Robot.SequenceRemainingMs/1000.0):
                motorSteps.Count>0?String.Format("{0} bước · {1:0.###} giây · chạy một lượt",motorSteps.Count,motorSteps.Sum(s=>s.DurationMs)/1000.0):
                "Chưa có bước nào · tối đa 20 bước, mỗi bước 0,1–30 giây");
        }
        public void PauseControl() { CancelMotorInput(); Robot.StopAndRelease(); }
        private void WireEvents() {
            Click("Refresh",RefreshPorts);
            Click("Connect",delegate {
                if (uploader.Running) return;
                Robot.Connect(E<ComboBox>("Port").Text.Trim().ToUpperInvariant());
            });
            Click("Disconnect",delegate { heldKeys.Clear(); mouseMotion=false; Robot.Disconnect(); Inform("Đã ngắt USB."); });
            Click("Claim",delegate { Robot.Claim(); });
            Click("Release",delegate { heldKeys.Clear(); Robot.StopAndRelease(); });
            Click("Emergency",delegate { heldKeys.Clear(); mouseMotion=false; Robot.EmergencyStop(); });
            Click("Stop",delegate { heldKeys.Clear(); mouseMotion=false; Robot.Stop(); });
            Click("Arm",delegate { heldKeys.Clear(); mouseMotion=false; Robot.Arm(Check("WheelsClear")); });
            E<CheckBox>("WheelsClear").Click+=delegate {
                if (!Check("WheelsClear") && Robot.Armed) { CancelMotorInput(); Robot.Stop(); }
                Update();
            };
            E<CheckBox>("ArmsClear").Click+=delegate { Update(); };
            Click("ResetEstop",delegate {
                if (!Check("WheelsClear")) throw new InvalidOperationException("Xác nhận vùng chuyển động an toàn trước khi reset dừng khẩn.");
                Robot.Command("reset_estop","confirm","WHEELS_LIFTED");
                Robot.Command("usb_status");
            });
            Click("NavDrive",delegate { Navigate("Drive","4 động cơ DC độc lập"); });
            Click("NavArms",delegate { Navigate("Arms","Hai cánh tay"); });
            Click("NavEyes",delegate { Navigate("Eyes","Mắt và âm thanh"); });
            Click("NavAi",delegate { Navigate("Ai","Trợ lý AI bằng giọng nói"); });
            Click("NavSettings",delegate { Navigate("Settings","Cấu hình & chẩn đoán"); });
            WireMotorControls();
            Click("ServoOn",delegate { Robot.EnableServos(true,Check("ArmsClear")); });
            Click("ServoOff",delegate { Robot.EnableServos(false,false); });
            Click("ArmsHome",delegate { SetArms(leftHome,rightHome); });
            Click("BothUp",delegate { SetArms(E<Slider>("LeftArm").Maximum,E<Slider>("RightArm").Maximum); });
            Click("BothDown",delegate { SetArms(E<Slider>("LeftArm").Minimum,E<Slider>("RightArm").Minimum); });
            Click("LeftUp",delegate { SetArms(E<Slider>("LeftArm").Maximum,E<Slider>("RightArm").Value); });
            Click("LeftDown",delegate { SetArms(E<Slider>("LeftArm").Minimum,E<Slider>("RightArm").Value); });
            Click("RightUp",delegate { SetArms(E<Slider>("LeftArm").Value,E<Slider>("RightArm").Maximum); });
            Click("RightDown",delegate { SetArms(E<Slider>("LeftArm").Value,E<Slider>("RightArm").Minimum); });
            foreach (string id in new[]{"LeftArm","RightArm"}) E<Slider>(id).ValueChanged+=delegate {
                Text("LeftAngle",E<Slider>("LeftArm").Value.ToString("0") + "°");
                Text("RightAngle",E<Slider>("RightArm").Value.ToString("0") + "°");
                if (!loadingSettings) armsDirty=true;
            };
            string[,] expressions={
                {"normal","Bình thường"},{"happy","Vui vẻ"},{"sad","Buồn"},
                {"angry","Giận"},{"surprised","Ngạc nhiên"},{"blink","Chớp mắt"},
                {"sleep","Ngủ"},{"listening","Lắng nghe"},{"thinking","Suy nghĩ"},
                {"speaking","Đang nói"},{"error","Lỗi"},{"low_battery","Pin yếu"}
            };
            for (int i=0;i<expressions.GetLength(0);i++) {
                string value=expressions[i,0],label=expressions[i,1];
                var button=new Button { Content=label,Padding=new Thickness(8,11,8,11),FontSize=12 };
                button.Click+=delegate {
                    expression=value; PreviewEyes();
                    Text("ExpressionLabel",label + " · xem trước");
                    if (Robot.Ready && Robot.Owned && Robot.LcdEnabled) Robot.Command("expression","value",value);
                };
                E<UniformGrid>("ExpressionButtons").Children.Add(button);
            }
            foreach (string id in new[]{"LookX","LookY"}) E<Slider>(id).ValueChanged+=delegate { eyesDirty=true; PreviewEyes(); };
            E<Slider>("Brightness").ValueChanged+=delegate { if (!loadingSettings) optionsDirty=true; };
            E<CheckBox>("Blink").Click+=delegate { optionsDirty=true; };
            Click("Tone",delegate { SendAudio("tone"); });
            Click("AudioStop",delegate { SendAudio("stop"); });
            Click("MicStart",delegate { SendAudio("capture_start"); });
            Click("MicStop",delegate { SendAudio("capture_stop"); });
            Click("ReloadSettings",delegate { lastSettings=null; Robot.Command("usb_settings_get"); });
            E<Slider>("MotorLimit").ValueChanged+=delegate { Text("LimitLabel","Giới hạn PWM motor: " + E<Slider>("MotorLimit").Value.ToString("0") + "%"); };
            Click("SaveSettings",SaveConfiguration);
            Click("AiSaveWifi",SaveAiWifi);
            Click("AiSave",SaveAiConfiguration);
            Click("AiPause",delegate { Robot.PauseAi(true); });
            Click("AiResume",delegate { Robot.PauseAi(false); });
            Click("AiReboot",delegate {
                Robot.Reboot(); Robot.Disconnect();
                Inform("Đã yêu cầu khởi động lại. Robot sẽ tự nối Wi-Fi và nghe nếu đã bật AI. Kết nối USB lại để xem trạng thái.");
            });
            E<Slider>("AiThreshold").ValueChanged+=delegate {
                Text("AiThresholdLabel","Ngưỡng nhận giọng: " + E<Slider>("AiThreshold").Value.ToString("0"));
            };
            Click("Reboot",delegate {
                if (MessageBox.Show(Window,"Khởi động lại ESP32 để áp dụng cấu hình? Chuyển động sẽ bị khóa.",
                        "Khởi động lại",MessageBoxButton.YesNo,MessageBoxImage.Question)==MessageBoxResult.Yes) {
                    Robot.Reboot(); Robot.Disconnect(); Inform("Đã yêu cầu khởi động lại. Bấm Kết nối sau vài giây.");
                }
            });
            Click("UpdateFirmware",StartUpload);
            Window.PreviewKeyDown+=delegate(object sender,KeyEventArgs e) {
                if (e.Key==Key.Escape) {
                    heldKeys.Clear(); mouseMotion=false; Robot.EmergencyStop(); e.Handled=true; return;
                }
                if (page!="Drive" || !Robot.Armed || Robot.SequenceRunning || !Window.IsActive || IsEditing() || MotorKeyIndex(e.Key)<0 || mouseMotion) return;
                if (e.IsRepeat) { e.Handled=true; return; }
                heldKeys[e.Key]=(Keyboard.Modifiers & ModifierKeys.Shift)!=0?-1:1;
                KeyboardMotion(); e.Handled=true;
            };
            Window.PreviewKeyUp+=delegate(object sender,KeyEventArgs e) {
                if (heldKeys.Remove(e.Key)) { KeyboardMotion(); e.Handled=true; }
            };
            Window.PreviewGotKeyboardFocus+=delegate(object sender,KeyboardFocusChangedEventArgs e) {
                if ((e.NewFocus is TextBoxBase || e.NewFocus is ComboBox || e.NewFocus is Slider) &&
                    (mouseMotion || heldKeys.Count>0)) CancelMotorInput();
            };
        }
        private bool IsEditing() {
            return Keyboard.FocusedElement is TextBoxBase || Keyboard.FocusedElement is ComboBox ||
                   Keyboard.FocusedElement is Slider;
        }
        private static int MotorKeyIndex(Key key) {
            if (key>=Key.D1 && key<=Key.D4) return (int)key-(int)Key.D1;
            if (key>=Key.NumPad1 && key<=Key.NumPad4) return (int)key-(int)Key.NumPad1;
            return -1;
        }
        private void KeyboardMotion() {
            double[] values=new double[4];
            foreach (var entry in heldKeys) values[MotorKeyIndex(entry.Key)]+=entry.Value;
            for (int i=0;i<4;i++) values[i]=Math.Sign(values[i])*MotorLevel(i);
            if (heldKeys.Count==0) Robot.ReleaseMotion(); else Robot.SetMotors(values);
        }
        private void MotorHold(string id,Func<double[]> values) {
            Button button=E<Button>(id);
            button.PreviewMouseLeftButtonDown+=delegate(object sender,MouseButtonEventArgs e) {
                if (!Robot.Armed || Robot.SequenceRunning || !Window.IsActive || page!="Drive") return;
                heldKeys.Clear(); mouseMotion=true;
                if (!motorPointer.TryCapture(button)) { mouseMotion=false; Robot.ReleaseMotion(); return; }
                Robot.SetMotors(values()); e.Handled=true;
            };
            button.PreviewMouseLeftButtonUp+=delegate(object sender,MouseButtonEventArgs e) {
                if (!mouseMotion) return;
                mouseMotion=false; motorPointer.Release(button); Robot.ReleaseMotion(); e.Handled=true;
            };
            button.LostMouseCapture+=delegate {
                if (mouseMotion) { mouseMotion=false; Robot.ReleaseMotion(); }
            };
        }
        private void Navigate(string name,string title) {
            CancelMotorInput(); Robot.Stop();
            page=name; Text("PageTitle",title);
            foreach (string item in new[]{"Drive","Arms","Eyes","Ai","Settings"}) {
                E<Grid>(item + "Page").Visibility=item==name ? Visibility.Visible : Visibility.Collapsed;
                E<Button>("Nav" + item).Background=(Brush)new BrushConverter().ConvertFromString(item==name?"#2E3A4C":"#171F2C");
            }
        }
        private void RefreshPorts() {
            string selected=E<ComboBox>("Port").Text;
            string[] ports=SerialPort.GetPortNames().OrderBy(x=>x).ToArray();
            E<ComboBox>("Port").ItemsSource=ports;
            E<ComboBox>("Port").Text=ports.Contains(selected)?selected:ports.Contains("COM6")?"COM6":String.IsNullOrEmpty(selected)?"COM6":selected;
        }
        private void SetArms(double left,double right) {
            E<Slider>("LeftArm").Value=left; E<Slider>("RightArm").Value=right;
            armsDirty=true;
        }
        private void FlushControls() {
            long time=clock.ElapsedMilliseconds;
            if (armsDirty && time>=nextArms) {
                armsDirty=false; nextArms=time+100;
                Robot.SetArms(E<Slider>("LeftArm").Value,E<Slider>("RightArm").Value);
            }
            if (eyesDirty && time>=nextEyes) {
                eyesDirty=false; nextEyes=time+120;
                if (Robot.Ready && Robot.Owned && Robot.LcdEnabled)
                    Robot.Command("look","x",E<Slider>("LookX").Value/100,"y",E<Slider>("LookY").Value/100);
            }
            if (optionsDirty && time>=nextEyes) {
                optionsDirty=false; nextEyes=time+120;
                if (Robot.Ready && Robot.Owned && Robot.LcdEnabled)
                    Robot.Command("eyes_options","auto_blink",Check("Blink"),"brightness",(int)E<Slider>("Brightness").Value);
            }
        }
        private void SendAudio(string action) {
            if (!Robot.Ready || !Robot.Owned || !Robot.AudioEnabled)
                throw new InvalidOperationException("Bật audio trong Cấu hình, khởi động lại và nhận quyền trước.");
            Robot.Command("audio","action",action,"volume",(int)E<Slider>("Volume").Value);
        }
        private void SaveConfiguration() {
            double ratio;
            if (!Double.TryParse(E<TextBox>("BatteryRatio").Text.Replace(',','.'),NumberStyles.Float,CultureInfo.InvariantCulture,out ratio) ||
                Double.IsNaN(ratio)||Double.IsInfinity(ratio)||ratio<3||ratio>10)
                throw new InvalidOperationException("Tỷ số chia áp phải trong khoảng 3 đến 10.");
            Robot.SaveSettings(new Dictionary<string,object> {
                {"hardware_confirmed",Check("HardwareConfirmed")},{"lcd_enabled",Check("LcdEnabled")},
                {"audio_enabled",Check("AudioEnabled")},{"battery_enabled",Check("BatteryEnabled")},
                {"motor_limit",E<Slider>("MotorLimit").Value/100},{"battery_ratio",ratio},
                {"brightness",(int)E<Slider>("Brightness").Value},{"volume",(int)E<Slider>("Volume").Value}
            });
        }
        private void SaveAiWifi() {
            string ssid=E<TextBox>("AiSsid").Text.Trim();
            if (Encoding.UTF8.GetByteCount(ssid)==0 || Encoding.UTF8.GetByteCount(ssid)>32)
                throw new InvalidOperationException("Tên Wi-Fi phải có từ 1 đến 32 byte UTF-8.");
            string password=E<PasswordBox>("AiWifiPassword").Password;
            if (password.Length>0 && (Encoding.UTF8.GetByteCount(password)<8 || Encoding.UTF8.GetByteCount(password)>63))
                throw new InvalidOperationException("Mật khẩu Wi-Fi phải có từ 8 đến 63 byte.");
            var patch=new Dictionary<string,object> {
                {"wifi_ssid",ssid},{"audio_enabled",Check("AiAudioEnabled")},
                {"mic_right",Check("AiMicRight")}
            };
            if (password.Length>0) patch["wifi_password"]=password;
            Robot.SaveSettings(patch);
            E<PasswordBox>("AiWifiPassword").Clear();
        }
        private void SaveAiConfiguration() {
            var config=new Dictionary<string,object> {
                {"enabled",Check("AiEnabled")},{"model",E<TextBox>("AiModel").Text.Trim()},
                {"voice",E<TextBox>("AiVoice").Text.Trim()},
                {"vad_threshold",(int)E<Slider>("AiThreshold").Value}
            };
            string key=E<PasswordBox>("AiApiKey").Password.Trim();
            if (Check("AiEraseKey")) {
                if (Check("AiEnabled"))
                    throw new InvalidOperationException("Bỏ chọn tự nghe AI khi xóa API key.");
                config["api_key"]="";
            } else if (key.Length>0) {
                if (!System.Text.RegularExpressions.Regex.IsMatch(key,@"^sk-[A-Za-z0-9_-]{17,252}$"))
                    throw new InvalidOperationException("API key không đúng định dạng. Nhập key mới trực tiếp vào ô mật khẩu.");
                config["api_key"]=key;
            } else if (Check("AiEnabled") && !RobotController.Flag(Robot.AiSettings,"key_configured"))
                throw new InvalidOperationException("Nhập API key trước khi bật tự nghe AI.");
            Robot.ConfigureAi(config);
            E<PasswordBox>("AiApiKey").Clear();
            E<CheckBox>("AiEraseKey").IsChecked=false;
        }
        private void LoadConfiguration() {
            if (Robot.Settings==null || Object.ReferenceEquals(lastSettings,Robot.Settings)) return;
            lastSettings=Robot.Settings; loadingSettings=true;
            try {
                var settings=Robot.Settings;
                E<CheckBox>("HardwareConfirmed").IsChecked=RobotController.Flag(settings,"hardware_confirmed");
                E<CheckBox>("LcdEnabled").IsChecked=RobotController.Flag(settings,"lcd_enabled");
                E<CheckBox>("AudioEnabled").IsChecked=RobotController.Flag(settings,"audio_enabled");
                E<CheckBox>("BatteryEnabled").IsChecked=RobotController.Flag(settings,"battery_enabled");
                E<Slider>("MotorLimit").Value=RobotController.Number(settings,"motor_limit",0.25)*100;
                E<TextBox>("BatteryRatio").Text=RobotController.Number(settings,"battery_ratio",4).ToString("0.####",CultureInfo.InvariantCulture);
                E<Slider>("Brightness").Value=RobotController.Number(settings,"brightness",80);
                E<Slider>("Volume").Value=RobotController.Number(settings,"volume",20);
                E<TextBox>("AiSsid").Text=RobotController.Text(settings,"wifi_ssid","");
                E<CheckBox>("AiAudioEnabled").IsChecked=RobotController.Flag(settings,"audio_enabled");
                E<CheckBox>("AiMicRight").IsChecked=RobotController.Flag(settings,"mic_right");
            } finally { loadingSettings=false; }
        }
        private void UpdateAi() {
            bool supported=Robot.Ready && Robot.AiSettings!=null;
            bool canSave=supported&&!Robot.Owned&&!Robot.Armed&&!Robot.ServoEnabled;
            foreach (string id in new[]{"AiSaveWifi","AiSave","AiPause"}) E<Button>(id).IsEnabled=canSave;
            E<Button>("AiResume").IsEnabled=canSave&&!Robot.RestartRequired;
            E<Button>("AiReboot").IsEnabled=supported;
            if (supported && !Object.ReferenceEquals(lastAiSettings,Robot.AiSettings)) {
                lastAiSettings=Robot.AiSettings;
                E<CheckBox>("AiEnabled").IsChecked=RobotController.Flag(lastAiSettings,"enabled");
                E<TextBox>("AiModel").Text=RobotController.Text(lastAiSettings,"model","gpt-realtime-2.1-mini");
                E<TextBox>("AiVoice").Text=RobotController.Text(lastAiSettings,"voice","marin");
                E<Slider>("AiThreshold").Value=RobotController.Number(lastAiSettings,"vad_threshold",250);
            }
            Text("AiKeyStatus",!supported?"Cần firmware 2.2 để cấu hình AI.":
                RobotController.Flag(Robot.AiSettings,"key_configured")?
                "ESP32 đã lưu key. Để trống ô này để giữ key hiện có.":"ESP32 chưa có API key.");
            object value;
            var ai=Robot.Telemetry!=null && Robot.Telemetry.TryGetValue("ai",out value)?value as Dictionary<string,object>:null;
            var labels=new Dictionary<string,string> {
                {"DISABLED","AI đang tắt"},{"WAIT_WIFI","Đang chờ Wi-Fi có Internet"},
                {"WAIT_TIME","Đang đồng bộ giờ để kết nối bảo mật"},{"CONNECTING","Đang kết nối OpenAI"},
                {"LISTENING","Đang nghe — bạn có thể nói"},{"RECORDING","Đang thu câu hỏi của bạn"},
                {"THINKING","AI đang trả lời"},{"SPEAKING","Đang phát câu trả lời qua loa"},
                {"PAUSED","AI tạm dừng — trả quyền điều khiển để tiếp tục"},
                {"AUDIO_NOT_READY","Chưa bật hoặc chưa khởi tạo được micro / loa"},
                {"RATE_LIMITED","Đã đạt giới hạn 30 câu hỏi trong giờ này"},{"ERROR","Lỗi kết nối AI"}
            };
            string code=RobotController.Text(ai,"state",""),label;
            Text("AiStatus",!supported?"Cần nạp firmware 2.2 và kết nối USB để cấu hình.":
                Robot.RestartRequired?"Đã lưu thay đổi. Khởi động lại ESP32 để áp dụng.":
                labels.TryGetValue(code,out label)?label:code);
            string error=RobotController.Text(ai,"error","");
            var errors=new Dictionary<string,string> {
                {"API_AUTH","API key không hợp lệ hoặc đã bị thu hồi. Nhập key mới và khởi động lại."},
                {"API_QUOTA","Kiểm tra hạn mức / thanh toán API rồi khởi động lại robot."},
                {"API_MODEL_ACCESS","Tài khoản chưa có quyền dùng model. Kiểm tra model rồi lưu và khởi động lại."},
                {"API_ERROR","OpenAI từ chối yêu cầu. Kiểm tra model và cấu hình; cần khởi động lại để thử lại."},
                {"TLS_OR_NETWORK","Kiểm tra Internet, DNS và ngày giờ. Robot sẽ thử lại sau 60 giây."},
                {"MIC_OVERRUN","Thu âm bị mất mẫu. Kiểm tra micro và nguồn; robot sẽ thử lại."}
            };
            if (errors.TryGetValue(error,out label)) Text("AiStatus",label);
            else if (error.Length>0) Text("AiStatus",E<TextBlock>("AiStatus").Text + " · " + error);
            var wifi=Robot.Telemetry!=null && Robot.Telemetry.TryGetValue("wifi",out value)?value as Dictionary<string,object>:null;
            Text("AiNetwork",RobotController.Flag(wifi,"station_connected")?
                "Wi-Fi đã kết nối · IP " + RobotController.Text(wifi,"station_ip",""):
                "Wi-Fi chưa kết nối. Lưu SSID / mật khẩu Wi-Fi 2.4 GHz rồi khởi động lại.");
            string reply=RobotController.Text(ai,"answer","");
            E<TextBox>("AiReply").Text=reply.Length>0?reply:"Câu trả lời sẽ xuất hiện ở đây khi đang kết nối USB.";
            if (!Robot.Ready) lastAiSettings=null;
        }
        private void UpdateArmLimits(Dictionary<string,object> data) {
            object value;
            if (data==null || !data.TryGetValue("servo_limits",out value)) return;
            var array=value as object[];
            if (array==null || array.Length!=2) return;
            loadingSettings=true;
            try {
                for (int i=0;i<2;i++) {
                    var limits=array[i] as Dictionary<string,object>;
                    string id=i==0?"Left":"Right";
                    double min=RobotController.Number(limits,"min_deg",60),max=RobotController.Number(limits,"max_deg",120);
                    var slider=E<Slider>(id+"Arm");
                    slider.Minimum=Math.Min(min,slider.Maximum); slider.Maximum=max; slider.Minimum=min;
                    if (!armLimitsLoaded) {
                        object commanded;
                        var actual=data.TryGetValue("servo_commanded_deg",out commanded)?commanded as object[]:null;
                        slider.Value=actual!=null && actual.Length==2 ? Convert.ToDouble(actual[i]) :
                                     RobotController.Number(limits,"home_deg",90);
                    }
                    if (i==0) leftHome=RobotController.Number(limits,"home_deg",90); else rightHome=RobotController.Number(limits,"home_deg",90);
                    Text(id+"Range",String.Format("Giới hạn: {0:0}° – {1:0}°",min,max));
                }
                armLimitsLoaded=true;
            } finally { loadingSettings=false; }
        }
        private void PreviewEyes() {
            var left=E<System.Windows.Shapes.Rectangle>("EyeLeft");
            var right=E<System.Windows.Shapes.Rectangle>("EyeRight");
            double height=expression=="sleep"||expression=="blink"?8:expression=="happy"?35:expression=="surprised"?72:60;
            foreach (var eye in new[]{left,right}) {
                eye.Height=height;
                Canvas.SetTop(eye,50-height/2);
                eye.Fill=(Brush)new BrushConverter().ConvertFromString(expression=="error"?"#F56C79":expression=="low_battery"?"#F4C348":"#55D7EF");
                eye.RenderTransformOrigin=new Point(0.5,0.5);
            }
            left.RenderTransform=new RotateTransform(expression=="angry"?16:expression=="sad"?-14:0);
            right.RenderTransform=new RotateTransform(expression=="angry"?-16:expression=="sad"?14:0);
            E<Canvas>("EyeGroup").RenderTransform=new TranslateTransform(E<Slider>("LookX").Value*0.15,-E<Slider>("LookY").Value*0.1);
        }
        private void Update() {
            bool ready=Robot.Ready,own=ready&&Robot.Owned,armed=own&&Robot.Armed;
            Text("ConnectionState",ready?"●  USB đã kết nối":Robot.Connected?"●  Đang chờ ESP32":"●  Chưa kết nối");
            E<TextBlock>("ConnectionState").Foreground=(Brush)new BrushConverter().ConvertFromString(ready?"#73D6AF":"#91A4BB");
            E<Button>("Connect").IsEnabled=!Robot.Connected&&!uploader.Running;
            E<Button>("Disconnect").IsEnabled=Robot.Connected;
            E<Button>("Claim").IsEnabled=ready&&!own&&!Robot.RestartRequired;
            E<Button>("Release").IsEnabled=own;
            E<ComboBox>("Port").IsEnabled=!Robot.Connected&&!uploader.Running;
            E<Button>("Refresh").IsEnabled=!Robot.Connected&&!uploader.Running;
            E<Button>("Emergency").IsEnabled=ready;
            E<Button>("Arm").IsEnabled=own&&!armed&&!Robot.RestartRequired&&Robot.HardwareConfirmed&&Check("WheelsClear");
            E<Button>("ResetEstop").IsEnabled=own&&Check("WheelsClear")&&RobotController.Text(Robot.Telemetry,"state","")=="EMERGENCY_STOP";
            E<Button>("Stop").IsEnabled=own;
            if (!armed) { heldKeys.Clear(); mouseMotion=false; }
            E<Button>("ServoOn").IsEnabled=own&&!Robot.RestartRequired&&!Robot.ServoEnabled&&Robot.HardwareConfirmed&&Check("ArmsClear");
            E<Button>("ServoOff").IsEnabled=own&&Robot.ServoEnabled;
            foreach (string id in new[]{"ArmsHome","BothUp","BothDown","LeftUp","LeftDown","RightUp","RightDown"}) E<Button>(id).IsEnabled=own&&Robot.ServoEnabled;
            E<Slider>("LeftArm").IsEnabled=own&&Robot.ServoEnabled;
            E<Slider>("RightArm").IsEnabled=own&&Robot.ServoEnabled;
            Text("ServoStatus",Robot.ServoEnabled?"SERVO ĐANG BẬT":"SERVO ĐANG KHÓA");
            foreach (string id in new[]{"Tone","AudioStop","MicStart","MicStop"}) E<Button>(id).IsEnabled=own&&Robot.AudioEnabled;
            E<Button>("SaveSettings").IsEnabled=ready&&!own&&!Robot.Armed&&!Robot.ServoEnabled;
            E<Button>("ReloadSettings").IsEnabled=ready;
            E<Button>("Reboot").IsEnabled=ready;
            Text("LockStatus",armed?"ĐỘNG CƠ ĐÃ MỞ KHÓA":"ĐỘNG CƠ ĐANG KHÓA");
            Text("HardwareHint",Robot.RestartRequired?"Đã lưu thay đổi. Khởi động lại ESP32 để áp dụng.":Robot.HardwareConfirmed?"Phần cứng đã được xác nhận trong cấu hình.":"Chỉ mở khóa sau khi xác nhận phần cứng trong Cấu hình.");
            Text("ConfigStatus",Robot.Settings==null?"Chưa có cấu hình từ ESP32.":Robot.RestartRequired?"Đã lưu. Cần khởi động lại ESP32.":"Đã đọc cấu hình từ ESP32; thay đổi chỉ được ghi khi bấm Lưu.");
            Text("LcdStatus",Robot.LcdEnabled?"LCD được bật. Nhận quyền để gửi biểu cảm.":"LCD chưa bật. Các hình mắt ở đây chỉ là xem trước.");
            Text("AudioStatus",Robot.AudioEnabled?"Audio được bật trong firmware.":"Âm thanh chưa được bật trong cấu hình.");
            var data=Robot.Telemetry;
            Text("RobotState",data==null?"OFFLINE":RobotController.Text(data,"state","—"));
            Text("StateReason",data==null?"Chưa kết nối với ESP32.":RobotController.Text(data,"reason","—"));
            Text("Memory",data==null?"—":String.Format(CultureInfo.InvariantCulture,"{0:0.00} MB",RobotController.Number(data,"free_psram",0)/1048576));
            Text("Uptime",data==null?"—":TimeSpan.FromSeconds(RobotController.Number(data,"uptime_s",0)).ToString(@"hh\:mm\:ss"));
            Text("PwmLimit",data==null?"PWM giới hạn: —":String.Format("PWM tối đa: {0:0}% × mức từng motor",RobotController.Number(data,"motor_limit",0)*100));
            object nested;
            var battery=data!=null&&data.TryGetValue("battery",out nested)?nested as Dictionary<string,object>:null;
            Text("Battery",RobotController.Flag(battery,"available")?String.Format("{0:0.00} V",RobotController.Number(battery,"voltage",0)):"Chưa có số đo");
            var audio=data!=null&&data.TryGetValue("audio",out nested)?nested as Dictionary<string,object>:null;
            E<ProgressBar>("MicPeak").Value=RobotController.Number(audio,"peak",0);
            Text("MicStatus",RobotController.Flag(audio,"capturing")?"Đang thu · peak " + RobotController.Number(audio,"peak",0).ToString("0"):"Chưa thu âm");
            if (data!=null && data.TryGetValue("motor_commanded",out nested) && nested is object[]) {
                var motors=(object[])nested;
                string[] names={"MotorLF","MotorLR","MotorRF","MotorRR"};
                string[] labels={"M1","M2","M3","M4"};
                for (int i=0;i<Math.Min(4,motors.Length);i++) {
                    double val=Convert.ToDouble(motors[i])*100;
                    E<ProgressBar>(names[i]).Value=Math.Abs(val);
                    Text(names[i]+"Text",String.Format("{0}   {1:+0.0;-0.0;0.0}%",labels[i],val));
                }
            }
            var modules=data!=null&&data.TryGetValue("modules",out nested)?nested as Dictionary<string,object>:null;
            if (modules!=null) Text("Modules",String.Join("   ·   ",modules.Select(item=>item.Key+": "+(Convert.ToString(item.Value)=="ESP_OK"?"khởi tạo OK":Convert.ToString(item.Value)=="ESP_ERR_NOT_SUPPORTED"?"đang tắt":Convert.ToString(item.Value)))));
            if (!ready) {
                lastSettings=null; armLimitsLoaded=false; heldKeys.Clear(); mouseMotion=false;
                E<CheckBox>("WheelsClear").IsChecked=false;
                E<CheckBox>("ArmsClear").IsChecked=false;
                foreach (string id in new[]{"MotorLF","MotorLR","MotorRF","MotorRR"}) E<ProgressBar>(id).Value=0;
            }
            UpdateArmLimits(data); LoadConfiguration(); UpdateAi(); UpdateMotorControls();
            Text("Footer",ready?(own?"USB · Bạn đang giữ quyền điều khiển":"USB · Chỉ xem trạng thái"):"Chưa kết nối · Chuyển động đang khóa");
        }
        private void StartUpload() {
            if (uploader.Running) return;
            string port=E<ComboBox>("Port").Text.Trim().ToUpperInvariant();
            if (MessageBox.Show(Window,"Nạp firmware hỗ trợ GUI vào " + port + "?\n\nĐể nguồn motor/servo tắt trong lần nạp. Nếu không kết nối được: giữ BOOT, bấm RESET rồi thả BOOT; quét lại cổng nếu COM thay đổi.",
                "Cập nhật firmware USB",MessageBoxButton.YesNo,MessageBoxImage.Question)!=MessageBoxResult.Yes) return;
            Robot.Disconnect(); Navigate("Settings","Nạp firmware qua USB");
            uploaded=false; uploader.Start(projectRoot,port);
            Inform("Đang build và nạp firmware vào " + port + "…"); Update();
        }
        private void PollUpload() {
            string line;
            int count=0;
            while (count++<40 && uploader.TryRead(out line)) {
                events.Add(line);
                while (events.Count>70) events.RemoveAt(0);
                E<TextBox>("EventLog").Text=String.Join(Environment.NewLine,events);
                E<TextBox>("EventLog").ScrollToEnd();
                Text("UploadStatus",line);
            }
            if (!uploaded && uploader.ExitCode.HasValue) {
                uploaded=true;
                if (uploader.ExitCode.Value==0) Inform("Đã nạp xong. Thả BOOT, bấm RESET một lần, quét cổng rồi Kết nối.");
                else Inform("Nạp chưa thành công. Xem log; kiểm tra BOOT/RESET, cổng COM và đóng Serial Monitor.");
                RefreshPorts(); Update();
            }
            E<Button>("UpdateFirmware").IsEnabled=!uploader.Running;
        }
        public void RenderPreview(string destination,string section=null) {
            Window.Width=1280; Window.Height=850;
            Window.Show();
            Window.UpdateLayout();
            if (section!=null) { E<FrameworkElement>(section).BringIntoView(); Window.UpdateLayout(); }
            var image=new RenderTargetBitmap(1280,850,96,96,PixelFormats.Pbgra32);
            image.Render(Window);
            var encoder=new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
            using (var stream=File.Create(destination)) encoder.Save(stream);
            Window.Close();
        }
    }

    public static class Program {
        [STAThread]
        public static int Main(string[] args) {
            try {
                bool preview=(args.Length==2 || args.Length==3) && args[0]=="--preview";
                var app=new Application { ShutdownMode=ShutdownMode.OnMainWindowClose };
                var window=new ControlWindow(preview);
                if (args.Length==1 && args[0]=="--ai")
                    ((Button)window.Window.FindName("NavAi")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                if (preview) {
                    if (args.Length==3) {
                        var tab=window.Window.FindName("Nav" + (args[2]=="Sequence"?"Drive":args[2])) as Button;
                        if (tab==null) throw new ArgumentException("Unknown preview page");
                        tab.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                    }
                    window.RenderPreview(Path.GetFullPath(args[1]),args.Length==3&&args[2]=="Sequence"?"MotorSequenceSection":null); return 0;
                }
                app.Run(window.Window);
                return 0;
            } catch (Exception ex) {
                if (args.Length>0) {
                    File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,".pio","gui-error.txt"),ex.ToString());
                } else MessageBox.Show(ex.Message,"WALL-E Control",MessageBoxButton.OK,MessageBoxImage.Error);
                return 1;
            }
        }
    }
}
