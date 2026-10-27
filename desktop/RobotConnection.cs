using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO.Ports;
using System.Text;
using System.Web.Script.Serialization;

namespace WallE {
    public sealed class MotorStep {
        private readonly double[] values;
        public double[] Values { get { return (double[])values.Clone(); } }
        public int DurationMs { get; private set; }
        public MotorStep(double[] commands,int durationMs) {
            if (commands==null || commands.Length!=4 || durationMs<100 || durationMs>30000)
                throw new ArgumentException("Mỗi bước cần 4 motor và thời gian 0,1–30 giây.");
            foreach (double value in commands)
                if (Double.IsNaN(value) || Double.IsInfinity(value) || value < -1 || value > 1)
                    throw new ArgumentOutOfRangeException("commands");
            values=(double[])commands.Clone(); DurationMs=durationMs;
        }
    }
    public interface IRobotWire : IDisposable {
        bool IsOpen { get; }
        string Error { get; }
        void Open(string port);
        void Write(string text);
        bool TryRead(out string text);
    }

    public sealed class SerialWire : IRobotWire {
        private SerialPort port;
        private readonly ConcurrentQueue<string> lines = new ConcurrentQueue<string>();
        private readonly StringBuilder buffer = new StringBuilder();
        private readonly object readLock = new object();
        private volatile string error;
        private bool discard;
        public bool IsOpen { get { return port != null && port.IsOpen; } }
        public string Error { get { return error; } }
        public void Open(string name) {
            Dispose();
            error = null;
            buffer.Clear();
            discard = false;
            string old; while (lines.TryDequeue(out old)) {}
            port = new SerialPort(name, 115200) {
                DtrEnable = false, RtsEnable = false, Handshake = Handshake.None,
                Encoding = Encoding.UTF8, ReadTimeout = 50, WriteTimeout = 80
            };
            port.DataReceived += Receive;
            port.Open();
        }
        private void Receive(object sender, SerialDataReceivedEventArgs args) {
            try {
                lock (readLock) {
                    string data = ((SerialPort)sender).ReadExisting();
                    foreach (char ch in data) {
                        if (ch == '\r' || ch == '\n') {
                            if (!discard && buffer.Length > 0) {
                                string line = buffer.ToString();
                                // Only structured GUI replies. Never retain raw pairing/console text.
                                if (line.StartsWith("GUI ", StringComparison.Ordinal) && lines.Count < 100)
                                    lines.Enqueue(line.Substring(4));
                            }
                            buffer.Clear(); discard = false;
                        } else if (!discard) {
                            if (buffer.Length < 16384) buffer.Append(ch);
                            else { buffer.Clear(); discard = true; }
                        }
                    }
                }
            } catch (Exception ex) { error = ex.Message; }
        }
        public void Write(string text) {
            if (!IsOpen) throw new InvalidOperationException("Cổng USB đã đóng.");
            port.Write(text);
        }
        public bool TryRead(out string text) { return lines.TryDequeue(out text); }
        public void Dispose() {
            SerialPort previous = port;
            port = null;
            if (previous != null) {
                previous.DataReceived -= Receive;
                try { previous.Close(); } catch {}
                previous.Dispose();
            }
        }
    }

    // All outgoing commands run on the WPF UI thread. If it freezes, both the
    // drive refresh and heartbeat stop; the firmware watchdogs remain authoritative.
    public sealed class RobotController : IDisposable {
        private readonly IRobotWire wire;
        private readonly Func<long> now;
        private readonly JavaScriptSerializer json = new JavaScriptSerializer();
        private readonly Dictionary<int,string> pending = new Dictionary<int,string>();
        private int sequence, stateBarrier;
        private long openedAt, lastReply, lastTick, nextHeartbeat, nextDrive, nextStatus;
        private bool waitingHello, claimPending;
        public bool Ready { get; private set; }
        public bool Owned { get; private set; }
        public bool Armed { get; private set; }
        public bool ServoEnabled { get; private set; }
        public bool HardwareConfirmed { get; private set; }
        public bool RestartRequired { get; private set; }
        public bool LcdEnabled { get; private set; }
        public bool AudioEnabled { get; private set; }
        public bool Connected { get { return wire.IsOpen; } }
        public double Throttle { get; private set; }
        public double Steering { get; private set; }
        private readonly double[] motorCommands = new double[4];
        public bool IndependentMotors { get; private set; }
        public double[] MotorCommands { get { return (double[])motorCommands.Clone(); } }
        private MotorStep[] sequenceSteps;
        private int sequenceIndex=-1;
        private long stepDeadline;
        public bool SequenceRunning { get { return sequenceSteps!=null; } }
        public int SequenceIndex { get { return sequenceIndex; } }
        public int SequenceCount { get { return sequenceSteps==null?0:sequenceSteps.Length; } }
        public long SequenceRemainingMs { get { return SequenceRunning?Math.Max(0,stepDeadline-now()):0; } }
        private double speed = 0.4;
        public double Speed {
            get { return speed; }
            set {
                if (Double.IsNaN(value) || Double.IsInfinity(value) || value<0 || value>1)
                    throw new ArgumentOutOfRangeException("value");
                speed=value;
            }
        }
        public string LastError { get; private set; }
        public Dictionary<string,object> Telemetry { get; private set; }
        public Dictionary<string,object> Settings { get; private set; }
        public Dictionary<string,object> AiSettings { get; private set; }
        public event Action<string> Notice;
        public event Action Changed;
        public RobotController(IRobotWire wire, Func<long> clock) { this.wire=wire; now=clock; }
        private void Notify(string value) { if (Notice != null) Notice(value); }
        private void ChangedState() { if (Changed != null) Changed(); }
        public static double Number(IDictionary<string,object> d, string key, double fallback) {
            object value;
            try { return d != null && d.TryGetValue(key,out value) ? Convert.ToDouble(value) : fallback; }
            catch { return fallback; }
        }
        public static bool Flag(IDictionary<string,object> d, string key) {
            object value; return d != null && d.TryGetValue(key,out value) && value is bool && (bool)value;
        }
        public static string Text(IDictionary<string,object> d, string key, string fallback) {
            object value; return d != null && d.TryGetValue(key,out value) && value != null ? Convert.ToString(value) : fallback;
        }
        public void Connect(string name) {
            Disconnect();
            if (!System.Text.RegularExpressions.Regex.IsMatch(name ?? "", @"^COM[1-9][0-9]{0,3}$",
                    System.Text.RegularExpressions.RegexOptions.IgnoreCase))
                throw new ArgumentException("Chọn cổng COM hợp lệ, ví dụ COM6.");
            try {
                wire.Open(name);
                sequence=0; stateBarrier=0; waitingHello=true;
                openedAt=lastReply=lastTick=now(); LastError=null;
                wire.Write("\n");
                Command("usb_hello");
                Notify("Đang kết nối " + name + "…");
                ChangedState();
            } catch { Disconnect(); throw; }
        }
        public int Command(string type, params object[] fields) {
            if (!wire.IsOpen) return 0;
            if (sequence >= 2147483600) { Fail("Hãy kết nối lại USB."); return 0; }
            var packet = new Dictionary<string,object> { {"v",1},{"seq",++sequence},{"type",type} };
            for (int i=0;i+1<fields.Length;i+=2) packet[(string)fields[i]]=fields[i+1];
            string encoded=json.Serialize(packet);
            if (Encoding.UTF8.GetByteCount(encoded)>1024) throw new ArgumentException("Lệnh quá dài.");
            int id=sequence;
            try {
                if (pending.Count>100) { Fail("ESP32 không trả lời lệnh; đã ngắt kết nối."); return 0; }
                pending[id]=type;
                wire.Write("gui " + encoded + "\n");
                return id;
            } catch (Exception ex) { Fail("Lỗi USB: " + ex.Message); return 0; }
        }
        public void Tick() {
            if (!wire.IsOpen) return;
            long entered=now();
            // Check the UI gap BEFORE consuming queued ACKs. Old replies must
            // never revive a held direction after a paused/frozen event loop.
            if ((Owned || Armed || ServoEnabled) && entered-lastTick>300) {
                Fail("Giao diện bị gián đoạn; đã ngắt và khóa điều khiển. Kết nối lại để tiếp tục.");
                return;
            }
            lastTick=entered;
            if (!String.IsNullOrEmpty(wire.Error)) { Fail(wire.Error); return; }
            string line;
            for (int i=0;i<40 && wire.TryRead(out line);i++) {
                try {
                    var data=json.DeserializeObject(line) as Dictionary<string,object>;
                    if (data!=null) Receive(data);
                } catch (ArgumentException) {}
                  catch (InvalidOperationException) {}
            }
            if (!wire.IsOpen) return;
            long time=now();
            if (waitingHello && time-openedAt>3500) {
                Fail("Chưa nhận giao thức GUI. Bấm Cập nhật firmware USB; sau khi nạp, thả BOOT và bấm RESET.");
                return;
            }
            if (!Ready) return;
            if (time-lastReply>1800) { Fail("ESP32 mất phản hồi. Điều khiển đã khóa; kết nối lại để tiếp tục."); return; }
            if (Owned && time>=nextHeartbeat) { Command("heartbeat"); nextHeartbeat=time+200; }
            if (Owned && Armed && SequenceRunning && time>=stepDeadline) {
                if (sequenceIndex+1>=sequenceSteps.Length) {
                    ReleaseMotion(); Notify("Đã chạy xong chuỗi; cả 4 motor đã nhận lệnh dừng.");
                } else {
                    ++sequenceIndex;
                    stepDeadline=time+sequenceSteps[sequenceIndex].DurationMs;
                    ApplyMotors(sequenceSteps[sequenceIndex].Values);
                    ChangedState();
                }
            }
            if (Owned && Armed && time>=nextDrive) {
                SendMotion();
                nextDrive=time+100;
            }
            if (time>=nextStatus) { Command("usb_status"); nextStatus=time+500; }
        }
        private void Receive(Dictionary<string,object> data) {
            if (Number(data,"v",0)!=1) return;
            int id=(int)Number(data,"seq",0);
            string type=Text(data,"type","");
            if (type=="hello" && waitingHello && id==1 && Text(data,"transport","")=="usb") {
                pending.Remove(id); waitingHello=false; Ready=true; lastReply=now();
                nextStatus=now()+500;
                Command("usb_status"); Command("usb_settings_get");
                Notify("Đã kết nối USB. Nhấn Nhận quyền khi sẵn sàng điều khiển.");
            } else if (Ready && type=="telemetry" && pending.ContainsKey(id) && pending[id]=="usb_status") {
                pending.Remove(id); lastReply=now(); Telemetry=data;
                HardwareConfirmed=Flag(data,"hardware_confirmed");
                RestartRequired=Flag(data,"restart_required");
                LcdEnabled=Flag(data,"lcd_enabled"); AudioEnabled=Flag(data,"audio_enabled");
                if (id>=stateBarrier) {
                    Owned=Flag(data,"usb_controller"); Armed=Owned && Flag(data,"armed");
                    ServoEnabled=Owned && Flag(data,"servo_enabled");
                    if (!Armed) ClearMotion();
                }
            } else if (Ready && type=="settings" && pending.ContainsKey(id) && pending[id]=="usb_settings_get") {
                pending.Remove(id); lastReply=now();
                object settings;
                if (data.TryGetValue("settings",out settings)) Settings=settings as Dictionary<string,object>;
                if (data.TryGetValue("ai_settings",out settings)) AiSettings=settings as Dictionary<string,object>;
            } else if (Ready && type=="ack") {
                string command;
                if (!pending.TryGetValue(id,out command)) return;
                pending.Remove(id); lastReply=now();
                if (id<stateBarrier && (command=="claim" || command=="arm" || command=="disarm" ||
                    command=="release" || command=="estop" || command=="servo_enable" ||
                    command=="drive" || command=="motors" || command=="heartbeat")) {
                    ChangedState(); return;
                }
                bool ok=Flag(data,"ok");
                if (!ok) {
                    if (command=="usb_status" || command=="usb_settings_get") {
                        Fail("Phiên USB đã hết hiệu lực. Kết nối lại để đọc trạng thái mới.");
                        return;
                    }
                    if (command=="drive" || command=="motors" || command=="arm") {
                        Armed=false; ClearMotion();
                        if (command!="arm") stateBarrier=Command("disarm");
                    }
                    if (command=="claim") { Owned=false; claimPending=false; }
                    if (command=="servo_enable") ServoEnabled=false;
                    LastError=Text(data,"message",Text(data,"result","Lệnh bị từ chối."));
                    Notify("ESP32: " + LastError);
                    if (command=="heartbeat") StopAndRelease();
                } else {
                    if (command=="claim") { claimPending=false; Owned=true; nextHeartbeat=now(); Notify("Đã nhận quyền. Động cơ vẫn khóa."); }
                    if (command=="arm") {
                        Armed=true; ClearMotion(); IndependentMotors=false; stateBarrier=id;
                        Command("drive","throttle",0,"steering",0,"speed",Speed);
                        Command("usb_status");
                    }
                    if (command=="disarm" || command=="release" || command=="estop") {
                        Armed=false; ServoEnabled=false; ClearMotion(); stateBarrier=id;
                        if (command=="release") Owned=false;
                    }
                    if (command=="usb_settings_patch" || command=="usb_ai_config") {
                        RestartRequired=true; Notify("Đã lưu cấu hình. Khởi động lại để áp dụng.");
                        Command("usb_settings_get");
                    }
                }
            } else return;
            ChangedState();
        }
        private void Fail(string message) {
            LastError=message; Disconnect(); Notify(message);
        }
        private void ZeroTargets() { Throttle=0; Steering=0; Array.Clear(motorCommands,0,4); }
        private void ClearMotion() {
            ZeroTargets(); sequenceSteps=null; sequenceIndex=-1; stepDeadline=0;
        }
        private void SendMotion() {
            if (IndependentMotors) Command("motors","values",MotorCommands);
            else Command("drive","throttle",Throttle,"steering",Steering,"speed",Speed);
        }
        public void SetMotors(params double[] values) {
            if (!Ready || !Owned || !Armed) { ClearMotion(); return; }
            if (values==null || values.Length!=4) {
                ReleaseMotion(); throw new ArgumentException("Cần đủ 4 giá trị cho M1 đến M4.");
            }
            foreach (double value in values) {
                if (Double.IsNaN(value) || Double.IsInfinity(value) || value < -1 || value > 1) {
                    ReleaseMotion(); throw new ArgumentOutOfRangeException("values","Mức motor phải trong khoảng -100% đến +100%.");
                }
            }
            ClearMotion(); ApplyMotors(values);
        }
        private void ApplyMotors(double[] values) {
            ZeroTargets(); IndependentMotors=true;
            Array.Copy(values,motorCommands,4);
            SendMotion(); nextDrive=now()+100;
        }
        public void StartSequence(IEnumerable<MotorStep> steps) {
            if (!Ready || !Owned || !Armed || RestartRequired || SequenceRunning)
                throw new InvalidOperationException("Mở khóa motor và dừng chuỗi hiện tại trước khi chạy chuỗi mới.");
            var copy=new List<MotorStep>();
            if (steps==null) throw new ArgumentNullException("steps");
            foreach (var step in steps) {
                if (step==null || copy.Count>=20) throw new ArgumentException("Chuỗi tối đa 20 bước.");
                copy.Add(new MotorStep(step.Values,step.DurationMs));
            }
            if (copy.Count==0) throw new ArgumentException("Thêm ít nhất một bước trước khi chạy chuỗi.");
            ReleaseMotion();
            if (!Ready || !Owned || !Armed)
                throw new InvalidOperationException("Kết nối bị gián đoạn; chuỗi đã hủy và cần mở khóa lại.");
            sequenceSteps=copy.ToArray(); sequenceIndex=0;
            stepDeadline=now()+sequenceSteps[0].DurationMs;
            ApplyMotors(sequenceSteps[0].Values);
            ChangedState();
        }
        public void SetMotion(double throttle,double steering) {
            if (!Ready || !Owned || !Armed) { ClearMotion(); return; }
            if (Double.IsNaN(throttle) || Double.IsInfinity(throttle) ||
                Double.IsNaN(steering) || Double.IsInfinity(steering)) {
                ReleaseMotion(); return;
            }
            sequenceSteps=null; sequenceIndex=-1;
            Throttle=Math.Max(-1,Math.Min(1,throttle));
            Steering=Math.Max(-1,Math.Min(1,steering));
            if (IndependentMotors) {
                Array.Clear(motorCommands,0,4); IndependentMotors=false;
            }
            nextDrive=now();
        }
        public void ReleaseMotion() {
            bool wasSequence=SequenceRunning;
            ClearMotion();
            if (Ready && Owned && Armed) {
                SendMotion();
                nextDrive=now()+100;
            }
            if (wasSequence) ChangedState();
        }
        public void Claim() {
            if (Ready && !Owned && !claimPending) { claimPending=true; stateBarrier=Command("claim"); }
        }
        public void Arm(bool motorsClear) {
            if (!Ready || !Owned || !motorsClear || !HardwareConfirmed || RestartRequired)
                throw new InvalidOperationException("Cần nhận quyền, xác nhận vùng quay motor an toàn và hoàn tất cấu hình phần cứng.");
            // Preserve the confirmation token understood by existing firmware.
            ClearMotion(); stateBarrier=Command("arm","confirm","WHEELS_LIFTED");
        }
        public void Stop() {
            ClearMotion(); Armed=false; ServoEnabled=false;
            bool cancelClaim=claimPending && !Owned;
            if (Ready && (Owned || claimPending)) stateBarrier=Command("disarm");
            if (Ready && cancelClaim) stateBarrier=Command("release");
            claimPending=false;
            ChangedState();
        }
        public void StopAndRelease() {
            Stop();
            if (Ready && Owned) stateBarrier=Command("release");
            Owned=false; ChangedState();
        }
        public void EmergencyStop() {
            ClearMotion(); Armed=false; ServoEnabled=false;
            if (Ready) {
                stateBarrier=Command("estop");
                if (stateBarrier>0) Notify("Đã gửi yêu cầu dừng khẩn; cần reset riêng trước khi mở khóa.");
            } else Notify("Chưa kết nối ESP32; không gửi được lệnh dừng khẩn.");
            ChangedState();
        }
        public void EnableServos(bool enabled,bool confirmed) {
            if (!Ready || !Owned || (enabled && (!confirmed || !HardwareConfirmed || RestartRequired)))
                throw new InvalidOperationException("Cần nhận quyền và xác nhận tay an toàn.");
            stateBarrier=enabled ? Command("servo_enable","enabled",true,"confirm","ARMS_CLEAR")
                                 : Command("servo_enable","enabled",false);
            if (!enabled) ServoEnabled=false;
            // Only authoritative telemetry enables servo controls.
            Command("usb_status");
        }
        public void SetArms(double left,double right) {
            if (!Ready || !Owned || !ServoEnabled) return;
            object raw;
            var limits=Telemetry!=null && Telemetry.TryGetValue("servo_limits",out raw) ? raw as object[] : null;
            if (limits==null || limits.Length!=2) throw new InvalidOperationException("Chưa có giới hạn servo từ firmware.");
            double[] values={left,right};
            for (int i=0;i<2;i++) {
                var range=limits[i] as Dictionary<string,object>;
                if (Double.IsNaN(values[i]) || Double.IsInfinity(values[i]) ||
                    values[i]<Number(range,"min_deg",60) || values[i]>Number(range,"max_deg",120))
                    throw new ArgumentOutOfRangeException("left", "Góc nằm ngoài giới hạn servo.");
            }
            Command("arms","left_deg",left,"right_deg",right);
        }
        public void SaveSettings(Dictionary<string,object> patch) {
            if (!Ready || Owned || Armed || ServoEnabled)
                throw new InvalidOperationException("Dừng và trả quyền điều khiển trước khi lưu cấu hình.");
            Command("usb_settings_patch","patch",patch);
        }
        public void Reboot() {
            StopAndRelease();
            if (Ready) Command("usb_reboot");
        }
        public void ConfigureAi(Dictionary<string,object> config) {
            if (!Ready || Owned || Armed || ServoEnabled || AiSettings==null)
                throw new InvalidOperationException("Cần firmware 2.2, dừng và trả quyền trước khi lưu AI.");
            Command("usb_ai_config","config",config);
        }
        public void PauseAi(bool paused) {
            if (!Ready || Owned || Armed || ServoEnabled || AiSettings==null || (!paused && RestartRequired))
                throw new InvalidOperationException("Trả quyền và hoàn tất khởi động lại trước khi đổi chế độ nghe AI.");
            Command("usb_ai_pause","paused",paused);
        }
        public void Disconnect() {
            bool release=wire.IsOpen && Ready && (Owned || claimPending);
            // Clear local readiness before any best-effort write. A failed write
            // may re-enter Disconnect through Fail; it must never recurse again.
            Ready=false; Owned=false; Armed=false; ServoEnabled=false; waitingHello=false; claimPending=false;
            if (release) {
                // Best effort only; firmware timeouts handle unplugging and crashed GUIs.
                try {
                    Command("disarm"); Command("release");
                } catch {}
            }
            Ready=false; Owned=false; Armed=false; ServoEnabled=false; waitingHello=false;
            HardwareConfirmed=false; RestartRequired=false; Telemetry=null; Settings=null; AiSettings=null;
            IndependentMotors=false;
            ClearMotion(); pending.Clear(); wire.Dispose(); ChangedState();
        }
        public void Dispose() { Disconnect(); }
    }
}
