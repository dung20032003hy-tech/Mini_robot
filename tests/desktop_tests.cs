using System;
using System.Collections.Generic;
using System.Linq;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Threading;
using WallE;

sealed class ScriptedWire : IRobotWire {
    public bool IsOpen { get; private set; }
    public string Error { get; set; }
    public bool Silent, ThrowWrites, Confirmed=true, Owner, Armed, Servos;
    public bool AiSupported=true, KeyConfigured;
    public readonly List<Dictionary<string,object>> Sent=new List<Dictionary<string,object>>();
    private readonly Queue<string> incoming=new Queue<string>();
    private readonly JavaScriptSerializer json=new JavaScriptSerializer();
    public void Open(string name) { IsOpen=true; incoming.Clear(); }
    public void Dispose() { IsOpen=false; incoming.Clear(); }
    public bool TryRead(out string value) {
        if (incoming.Count==0) { value=null; return false; }
        value=incoming.Dequeue(); return true;
    }
    public void Inject(object data) { incoming.Enqueue(json.Serialize(data)); }
    public void Write(string text) {
        if (ThrowWrites) throw new InvalidOperationException("USB disconnected");
        if (!text.StartsWith("gui ")) return;
        var data=(Dictionary<string,object>)json.DeserializeObject(text.Substring(4));
        Sent.Add(data);
        if (Silent) return;
        int seq=Convert.ToInt32(data["seq"]);
        string type=(string)data["type"];
        if (type=="usb_hello") {
            Owner=Armed=Servos=false;
            Inject(new { v=1,seq=seq,type="hello",transport="usb" }); return;
        }
        if (type=="usb_status") {
            Inject(new { v=1,seq=seq,type="telemetry",hardware_confirmed=Confirmed,
                restart_required=false,usb_controller=Owner,armed=Armed,servo_enabled=Servos,
                lcd_enabled=false,audio_enabled=false,
                servo_limits=new[]{new{min_deg=60,max_deg=120,home_deg=90},new{min_deg=65,max_deg=115,home_deg=90}} });
            return;
        }
        if (type=="usb_settings_get") {
            if (AiSupported)
                Inject(new { v=1,seq=seq,type="settings",settings=new { hardware_confirmed=Confirmed,motor_limit=0.25 },
                    ai_settings=new {enabled=false,key_configured=KeyConfigured,model="gpt-realtime-2.1-mini",voice="marin",vad_threshold=250} });
            else Inject(new { v=1,seq=seq,type="settings",settings=new { hardware_confirmed=Confirmed,motor_limit=0.25 } });
            return;
        }
        if (type=="usb_ai_config") {
            var config=data["config"] as Dictionary<string,object>;
            if (config.ContainsKey("api_key")) KeyConfigured=((string)config["api_key"]).Length>0;
        }
        if (type=="claim") Owner=true;
        if (type=="arm") Armed=true;
        if (type=="disarm"||type=="release"||type=="estop") Armed=Servos=false;
        if (type=="release") Owner=false;
        if (type=="servo_enable") Servos=(bool)data["enabled"];
        Inject(new { v=1,seq=seq,type="ack",ok=true,result="ESP_OK" });
    }
}
sealed class Rig {
    public long Time;
    public readonly ScriptedWire Wire=new ScriptedWire();
    public readonly RobotController Robot;
    public Rig() { Robot=new RobotController(Wire,delegate{return Time;}); Robot.Connect("COM6"); Robot.Tick(); }
    public void Claim() { Robot.Claim(); Robot.Tick(); }
    public void Arm() { Claim(); Robot.Arm(true); Robot.Tick(); }
    public Dictionary<string,object> Last(string type) { return Wire.Sent.Last(x=>(string)x["type"]==type); }
}
// Windows mouse capture is an OS adapter, like the scripted serial wire. Tests
// route real WPF events without requiring a hardware pointer or an input desktop.
sealed class ScriptedPointer : IMotorPointerCapture {
    public Button Captured;
    public bool TryCapture(Button button) { ReleaseAll(); Captured=button; return true; }
    public void Release(Button button) { if (Captured==button) ReleaseAll(); }
    public void ReleaseAll() {
        Button previous=Captured; Captured=null;
        if (previous!=null) previous.RaiseEvent(new MouseEventArgs(Mouse.PrimaryDevice,Environment.TickCount) {
            RoutedEvent=Mouse.LostMouseCaptureEvent,Source=previous
        });
    }
}
static class Test {
    private static int checks;
    private static void Check(bool value,string message) { checks++; if (!value) throw new Exception(message); }
    private static void Throws(Action action,string message) {
        bool threw=false; try { action(); } catch (InvalidOperationException) { threw=true; }
        Check(threw,message);
    }
    private static double[] MotorValues(Dictionary<string,object> packet) {
        return ((object[])packet["values"]).Select(Convert.ToDouble).ToArray();
    }
    private static void MotorEquals(double[] actual,double[] expected,string message) {
        Check(actual.Length==4&&actual.Zip(expected,(a,b)=>Math.Abs(a-b)<0.000001).All(v=>v),message+"; actual="+String.Join(",",actual)+" expected="+String.Join(",",expected));
    }
    private static void MouseEvent(Button button,RoutedEvent routedEvent) {
        button.RaiseEvent(new MouseButtonEventArgs(Mouse.PrimaryDevice,Environment.TickCount,MouseButton.Left) {
            RoutedEvent=routedEvent,Source=button
        });
    }
    [STAThread]
    static int Main() {
        try {
            var r=new Rig();
            Check(r.Robot.Ready&&!r.Robot.Owned&&!r.Robot.Armed,"Connection must not auto claim or arm");
            Check(!r.Wire.Sent.Any(x=>(string)x["type"]=="arm"),"No implicit arm");
            r.Robot.SetMotion(1,1); Check(r.Robot.Throttle==0,"Offline movement gate");
            Throws(delegate{r.Robot.Arm(true);},"Arm requires ownership");
            r.Claim(); Throws(delegate{r.Robot.Arm(false);},"Arm requires confirmation");
            r.Wire.Confirmed=false; r.Robot.Command("usb_status"); r.Robot.Tick();
            Throws(delegate{r.Robot.Arm(true);},"Arm requires hardware confirmation");
            Console.WriteLine("PASS: connection, explicit claim and physical confirmation gates");

            r=new Rig(); r.Arm();
            Check(r.Robot.Armed,"Arm ACK enables motion");
            r.Robot.SetMotion(0.8,-0.3); r.Time+=101; r.Robot.Tick();
            Check(Convert.ToDouble(r.Last("drive")["throttle"])==0.8,"Held input transmitted");
            r.Robot.ReleaseMotion();
            Check(Convert.ToDouble(r.Last("drive")["throttle"])==0,"Release sends zero immediately");
            r.Time+=110; r.Robot.Tick();
            Check(Convert.ToDouble(r.Last("drive")["throttle"])==0,"Subsequent ticks cannot replay held motion");
            r.Robot.SetMotion(Double.NaN,1); r.Time+=110; r.Robot.Tick();
            Check(Convert.ToDouble(r.Last("drive")["throttle"])==0,"Non-finite motion cannot leak");
            Console.WriteLine("PASS: drive refresh, immediate release and invalid movement");

            r.Robot.SetMotion(1,0); r.Robot.StopAndRelease();
            Check(!r.Robot.Armed&&!r.Robot.Owned&&r.Robot.Throttle==0,"Focus loss stop/release state");
            Check((string)r.Wire.Sent.Last()["type"]=="release","Release must follow disarm");
            r.Claim(); Check(!r.Robot.Armed,"Reclaim cannot auto arm");
            r.Robot.Arm(true); r.Robot.Tick();
            Check(Convert.ToDouble(r.Last("drive")["throttle"])==0,"Re-arm starts with zero");
            int previous=0;
            foreach (var packet in r.Wire.Sent) {
                int seq=Convert.ToInt32(packet["seq"]); Check(seq>previous,"Strictly increasing sequence"); previous=seq;
            }
            Console.WriteLine("PASS: stop/release, explicit re-arm and sequence ordering");

            r=new Rig(); r.Arm(); r.Wire.Silent=true; r.Time+=1900; r.Robot.Tick();
            Check(!r.Robot.Connected&&!r.Robot.Ready&&!r.Robot.Armed,"Lost reply closes connection");
            r=new Rig(); r.Arm(); r.Wire.ThrowWrites=true;
            r.Robot.StopAndRelease();
            Check(!r.Robot.Connected&&!r.Robot.Owned,"Write failure must disconnect without recursion");
            r=new Rig(); r.Wire.Error="USB removed"; r.Robot.Tick();
            Check(!r.Robot.Ready,"Read failure disconnects");
            Console.WriteLine("PASS: unplug, reply timeout, reader failure and failed stop write");
            r=new Rig(); r.Arm(); r.Robot.Tick(); r.Wire.Silent=true;
            for (int i=0;i<21 && r.Robot.Connected;i++) { r.Time+=100; r.Robot.Tick(); }
            Check(!r.Robot.Connected&&!r.Robot.Armed,"Missing replies during a healthy UI loop still disconnect");
            Console.WriteLine("PASS: reply loss with regular ticks and queued ACKs cannot revive paused input");
            r=new Rig(); r.Robot.Claim(); r.Robot.StopAndRelease(); r.Robot.Tick();
            Check(!r.Robot.Owned&&!r.Robot.Armed,"Late claim ACK cannot restore ownership after losing focus");
            Check((string)r.Last("release")["type"]=="release","In-flight claim is explicitly released");
            r=new Rig(); r.Claim(); r.Robot.Arm(true); r.Robot.Stop(); r.Robot.Tick();
            Check(!r.Robot.Armed,"Late arm ACK cannot undo a newer stop");
            Check(!r.Wire.Sent.Any(x=>(string)x["type"]=="drive"),"A canceled arm cannot emit a drive on delayed ACK");
            Console.WriteLine("PASS: delayed claim and arm ACKs cannot undo stop/focus-loss barriers");

            r=new Rig(); r.Claim();
            Throws(delegate{r.Robot.SaveSettings(new Dictionary<string,object>{{"hardware_confirmed",true}});},"Cannot save while controlling");
            r.Robot.StopAndRelease(); r.Robot.Tick();
            r.Robot.SaveSettings(new Dictionary<string,object>{{"motor_limit",0.25}});
            Check((string)r.Wire.Sent.Last()["type"]=="usb_settings_patch","Explicit settings patch");
            r=new Rig();
            r.Robot.EmergencyStop();
            Check((string)r.Wire.Sent.Last()["type"]=="estop","Authenticated physical session can stop without owning");
            Console.WriteLine("PASS: settings interlock and emergency-stop accessibility");

            r=new Rig(); r.Claim();
            Throws(delegate{r.Robot.EnableServos(true,false);},"Servo requires explicit confirmation");
            r.Robot.EnableServos(true,true); r.Robot.Tick();
            Check(r.Robot.ServoEnabled,"Servo enable uses authoritative telemetry");
            int count=r.Wire.Sent.Count;
            bool rangeRejected=false; try { r.Robot.SetArms(90,150); } catch(ArgumentOutOfRangeException) {rangeRejected=true;}
            Check(rangeRejected&&r.Wire.Sent.Count==count,"Servo limits from firmware are enforced");
            r.Robot.SetArms(90,100);
            Check((string)r.Wire.Sent.Last()["type"]=="arms","Valid servo position accepted");
            r.Robot.Disconnect(); r.Robot.Connect("COM6"); r.Robot.Tick();
            Check(!r.Robot.Owned&&!r.Robot.ServoEnabled&&!r.Robot.Armed,"Reconnect starts fully disarmed");
            Console.WriteLine("PASS: servo limits, confirmation and reconnect");

            r=new Rig(); r.Robot.Disconnect(); r.Wire.Silent=true; r.Robot.Connect("COM6");
            r.Wire.Inject(new {v=1,seq=555,type="telemetry",armed=true,usb_controller=true});
            r.Robot.Tick(); Check(!r.Robot.Armed&&!r.Robot.Ready,"Unsolicited data cannot establish session");
            r.Time+=3501; r.Robot.Tick(); Check(!r.Robot.Connected,"Legacy firmware hello timeout");
            Console.WriteLine("PASS: unsupported firmware and unsolicited telemetry rejection");

            r=new Rig(); r.Claim();
            Throws(delegate { r.Robot.ConfigureAi(new Dictionary<string,object>{{"enabled",false}}); },"Cannot configure AI while holding motor lease");
            Throws(delegate { r.Robot.PauseAi(false); },"Cannot enable AI while holding motor lease");
            r.Robot.StopAndRelease(); r.Robot.Tick();
            r.Robot.ConfigureAi(new Dictionary<string,object>{{"enabled",false}}); r.Robot.Tick();
            Check(r.Robot.RestartRequired,"AI credentials require reboot before activation");
            Throws(delegate { r.Robot.PauseAi(false); },"Pending settings cannot be bypassed with AI resume");
            r.Robot.Disconnect(); r.Wire.AiSupported=false; r.Robot.Connect("COM6"); r.Robot.Tick();
            Check(r.Robot.AiSettings==null,"Old firmware cannot inherit cached AI capabilities");
            Throws(delegate { r.Robot.ConfigureAi(new Dictionary<string,object>{{"enabled",false}}); },"AI provisioning requires supported firmware");
            Console.WriteLine("PASS: AI provisioning interlocks, reboot requirement and legacy firmware detection");

            r=new Rig();
            r.Robot.SetMotors(1,1,1,1);
            Check(!r.Wire.Sent.Any(p=>(string)p["type"]=="motors"),"Unarmed motor vector cannot be sent");
            r.Arm(); r.Robot.SetMotors(0.4,0,0,0);
            MotorEquals(MotorValues(r.Last("motors")),new[]{0.4,0,0,0},"M1 runs independently; other motors receive zero");
            r.Time+=101; r.Robot.Tick();
            Check((string)r.Wire.Sent.Last()["type"]=="motors","Tick renews independent motors instead of overwriting with drive");
            MotorEquals(MotorValues(r.Last("motors")),new[]{0.4,0,0,0},"Independent hold persists across refresh");
            double[] vector={0.2,-0.4,0,0.7}; r.Robot.SetMotors(vector); vector[0]=1;
            MotorEquals(r.Robot.MotorCommands,new[]{0.2,-0.4,0,0.7},"Caller cannot mutate active motor targets");
            r.Robot.ReleaseMotion();
            MotorEquals(MotorValues(r.Last("motors")),new double[4],"Release immediately sends zero to all four motors");
            r.Time+=101; r.Robot.Tick();
            MotorEquals(MotorValues(r.Last("motors")),new double[4],"Released vector cannot replay on next tick");
            for (int combination=0;combination<81;combination++) {
                double[] expected=new double[4]; int encoded=combination;
                for (int i=0;i<4;i++) { expected[i]=(encoded%3-1)*(i+1)*0.2; encoded/=3; }
                r.Robot.SetMotors(expected); r.Robot.Tick();
                MotorEquals(MotorValues(r.Last("motors")),expected,"All 81 stop/forward/reverse combinations retain channel order");
            }
            bool invalid=false;
            try { r.Robot.SetMotors(0.5,Double.NaN,0,0); } catch (ArgumentOutOfRangeException) { invalid=true; }
            Check(invalid,"NaN motor value rejected");
            MotorEquals(MotorValues(r.Last("motors")),new double[4],"Invalid vector stops existing motion");
            r.Robot.Stop(); r.Robot.Tick(); r.Robot.Arm(true); r.Robot.Tick();
            Check(!r.Robot.IndependentMotors&&r.Robot.MotorCommands.All(v=>v==0),"Fresh arm cannot restore an earlier motor combination");
            Console.WriteLine("PASS: independent motor packets, all combinations, refresh, release, validation and fresh-arm gates");

            r=new Rig(); r.Arm();
            var steps=new[]{new MotorStep(new[]{0.3,0.5,0,0},300),new MotorStep(new[]{0,0,-0.4,0.2},200),new MotorStep(new double[4],100)};
            r.Robot.StartSequence(steps);
            steps[1]=new MotorStep(new[]{1.0,1,1,1},100); // Running sequence owns its snapshot.
            Check(r.Robot.SequenceRunning&&r.Robot.SequenceIndex==0,"Sequence starts at first explicit step");
            MotorEquals(MotorValues(r.Last("motors")),new[]{0.3,0.5,0,0},"First sequence vector applied immediately");
            for (int i=0;i<2;i++) { r.Time+=100; r.Robot.Tick(); }
            Check(r.Robot.SequenceIndex==0,"First step keeps its requested duration");
            r.Time+=100; r.Robot.Tick();
            Check(r.Robot.SequenceIndex==1,"Sequence advances at deadline");
            MotorEquals(MotorValues(r.Last("motors")),new[]{0,0,-0.4,0.2},"Next step replaces all channels with original snapshot");
            r.Time+=199; r.Robot.Tick(); Check(r.Robot.SequenceIndex==1,"Next step is not shortened");
            r.Time+=1; r.Robot.Tick();
            MotorEquals(MotorValues(r.Last("motors")),new double[4],"Zero-vector step provides a timed pause");
            Check(r.Robot.SequenceRunning&&r.Robot.SequenceIndex==2,"Timed pause remains a real sequence step");
            r.Time+=100; r.Robot.Tick(); Check(!r.Robot.SequenceRunning,"Sequence stops after one pass");
            MotorEquals(MotorValues(r.Last("motors")),new double[4],"Sequence completion sends zero immediately");
            r.Time+=200; r.Robot.Tick(); MotorEquals(r.Robot.MotorCommands,new double[4],"Completed sequence never auto loops");
            Console.WriteLine("PASS: timed motor sequence ordering, immutable snapshots, durations, pause and completion stop");

            r=new Rig(); r.Arm(); r.Robot.StartSequence(new[]{new MotorStep(new[]{0.4,0,0,0},100)});
            r.Robot.StopAndRelease(); r.Robot.Tick();
            Check(!r.Robot.SequenceRunning&&!r.Robot.Armed&&!r.Robot.Owned,"Focus loss cancels sequence and releases ownership");
            r.Claim(); r.Robot.Arm(true); r.Robot.Tick();
            Check(!r.Robot.SequenceRunning&&r.Robot.MotorCommands.All(v=>v==0),"Re-arm cannot resume interrupted sequence");
            r.Robot.StartSequence(new[]{new MotorStep(new[]{0.4,0,0,0},100),new MotorStep(new[]{0,0,0.5,0},100)});
            int beforePause=r.Wire.Sent.Count(p=>(string)p["type"]=="motors");
            r.Time+=301; r.Robot.Tick();
            Check(!r.Robot.Connected&&!r.Robot.SequenceRunning,"Paused UI cancels sequence before processing queued ACKs");
            Check(r.Wire.Sent.Count(p=>(string)p["type"]=="motors")==beforePause,"Paused UI never jumps through overdue motor steps");
            r=new Rig(); r.Arm(); r.Wire.Silent=true;
            r.Robot.StartSequence(new[]{new MotorStep(new[]{0.4,0,0,0},1000)});
            r.Wire.Inject(new {v=1,seq=Convert.ToInt32(r.Last("motors")["seq"]),type="ack",ok=false,result="ESP_ERR_INVALID_STATE"});
            r.Robot.Tick();
            Check(!r.Robot.Armed&&!r.Robot.SequenceRunning,"Rejected motor command cancels sequence and disarms");
            Check((string)r.Wire.Sent.Last()["type"]=="disarm","Rejected motor packet explicitly disarms firmware");
            Console.WriteLine("PASS: sequence cancellation on focus loss, stalled UI, delayed ACKs and firmware rejection");

            r=new Rig(); r.Arm(); r.Wire.Silent=true; r.Robot.SetMotors(0,0.5,0,0);
            int oldMotorSeq=Convert.ToInt32(r.Last("motors")["seq"]);
            r.Robot.Stop(); r.Wire.Silent=false; r.Robot.Arm(true); r.Robot.Tick();
            r.Wire.Inject(new {v=1,seq=oldMotorSeq,type="ack",ok=false,result="ESP_ERR_INVALID_STATE"}); r.Robot.Tick();
            Check(r.Robot.Armed&&!r.Robot.SequenceRunning,"Late motor ACK cannot undo a newer explicit arm");
            MotorEquals(r.Robot.MotorCommands,new double[4],"Late motor ACK cannot restore old channel power");
            bool bounded=false;
            try { new MotorStep(new double[4],99); } catch (ArgumentException) { bounded=true; }
            Check(bounded,"Sub-100 ms step rejected"); bounded=false;
            try { new MotorStep(new double[4],30001); } catch (ArgumentException) { bounded=true; }
            Check(bounded,"Over-30-second step rejected");
            int beforeInvalid=r.Wire.Sent.Count;
            bounded=false;
            try { r.Robot.StartSequence(Enumerable.Range(0,21).Select(i=>new MotorStep(new double[4],100))); }
            catch (ArgumentException) { bounded=true; }
            Check(bounded&&r.Wire.Sent.Count==beforeInvalid&&!r.Robot.SequenceRunning,"Oversized sequence rejected before sending any motion");
            r=new Rig(); r.Arm(); r.Wire.ThrowWrites=true;
            Throws(delegate {r.Robot.StartSequence(new[]{new MotorStep(new[]{0.5,0,0,0},1000)});},"Failed initial stop write cancels sequence start");
            Check(!r.Robot.Connected&&!r.Robot.SequenceRunning&&r.Robot.MotorCommands.All(v=>v==0),"Disconnect during sequence start cannot leave latent steps or targets");
            Console.WriteLine("PASS: stale motor ACK barriers and sequence duration/count bounds");

            new Application { ShutdownMode=ShutdownMode.OnExplicitShutdown };
            var uiWire=new ScriptedWire();
            long uiTime=0;
            var pointer=new ScriptedPointer();
            var ui=new ControlWindow(true,uiWire,delegate {return uiTime;},pointer);
            foreach (string page in new[]{"Drive","Arms","Eyes","Ai","Settings"}) {
                var button=(Button)ui.Window.FindName("Nav"+page);
                button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Check(((FrameworkElement)ui.Window.FindName(page+"Page")).Visibility==Visibility.Visible,"UI navigation "+page);
            }
            Check(((Button)ui.Window.FindName("Arm")).IsEnabled==false,"Offline arm is disabled");
            Check(((Button)ui.Window.FindName("Connect")).IsEnabled,"Connect remains available offline");
            ui.Robot.Connect("COM6"); ui.Robot.Tick(); ui.Robot.Claim(); ui.Robot.Tick();
            ((Button)ui.Window.FindName("NavDrive")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(!((Button)ui.Window.FindName("Arm")).IsEnabled,"Actual arm button requires checkbox");
            var clear=(CheckBox)ui.Window.FindName("WheelsClear"); clear.IsChecked=true;
            clear.RaiseEvent(new RoutedEventArgs(CheckBox.ClickEvent));
            Check(((Button)ui.Window.FindName("Arm")).IsEnabled,"Actual arm button enabled only with confirmed state");
            ((Button)ui.Window.FindName("Arm")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            ui.Robot.Tick(); Check(ui.Robot.Armed,"Actual arm event dispatches the guarded command");
            ((Button)ui.Window.FindName("NavArms")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(!ui.Robot.Armed,"Changing actual tab disarms motion");
            ui.PauseControl(); Check(!ui.Robot.Owned&&!ui.Robot.Armed,"Window pause releases the controller");
            ui.Robot.Tick();
            ((Button)ui.Window.FindName("NavDrive")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            ui.Robot.Claim(); ui.Robot.Tick();
            clear.IsChecked=true; clear.RaiseEvent(new RoutedEventArgs(CheckBox.ClickEvent));
            ((Button)ui.Window.FindName("Arm")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent)); ui.Robot.Tick();
            ui.Window.Show(); ui.Window.Activate(); ui.Window.UpdateLayout();
            ui.Window.Dispatcher.Invoke(DispatcherPriority.ApplicationIdle,new Action(delegate {}));
            Check(ui.Window.IsActive,"Interactive motor test window is active");
            Check(ui.Robot.Armed&&ui.Robot.Owned,"Motor window must stay armed after showing: ready="+ui.Robot.Ready+" owned="+ui.Robot.Owned+" armed="+ui.Robot.Armed);
            for (int channel=0;channel<4;channel++) {
                var forward=(Button)ui.Window.FindName("Motor"+(channel+1)+"Forward");
                Check(forward.IsEnabled,"Motor hold control enabled after arm");
                int motorDownEvents=0;
                forward.AddHandler(UIElement.PreviewMouseLeftButtonDownEvent,new MouseButtonEventHandler(delegate {++motorDownEvents;}),true);
                MouseEvent(forward,UIElement.PreviewMouseLeftButtonDownEvent);
                Check(pointer.Captured==forward&&motorDownEvents==1,"Motor hold uses pointer adapter after actual mouse-down event");
                double[] expected=new double[4]; expected[channel]=0.4;
                MotorEquals(ui.Robot.MotorCommands,expected,"Actual per-motor hold selects only requested channel");
                MouseEvent(forward,UIElement.PreviewMouseLeftButtonUpEvent);
                MotorEquals(ui.Robot.MotorCommands,new double[4],"Actual mouse release stops all channels");
                var reverse=(Button)ui.Window.FindName("Motor"+(channel+1)+"Reverse");
                MouseEvent(reverse,UIElement.PreviewMouseLeftButtonDownEvent);
                expected[channel]=-0.4;
                MotorEquals(ui.Robot.MotorCommands,expected,"Actual reverse hold inverts only requested channel");
                pointer.ReleaseAll();
                MotorEquals(ui.Robot.MotorCommands,new double[4],"Lost mouse capture stops motor hold");
            }
            var combo=(Button)ui.Window.FindName("RunCombination");
            int beforePreset=uiWire.Sent.Count;
            ((Button)ui.Window.FindName("Preset14")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(uiWire.Sent.Skip(beforePreset).Where(p=>(string)p["type"]=="motors").All(p=>MotorValues(p).All(v=>v==0)),"Selecting preset cannot start motors");
            ((ComboBox)ui.Window.FindName("Motor4Direction")).SelectedIndex=2;
            ((Slider)ui.Window.FindName("Motor4Level")).Value=60;
            MouseEvent(combo,UIElement.PreviewMouseLeftButtonDownEvent);
            MotorEquals(ui.Robot.MotorCommands,new[]{0.4,0,0,-0.6},"Actual mixed direction combination uses per-channel power");
            ((Slider)ui.Window.FindName("Motor1Level")).Value=50;
            MotorEquals(ui.Robot.MotorCommands,new double[4],"Editing power during a hold cancels motion and requires fresh press");
            ((TextBox)ui.Window.FindName("StepDuration")).Text="0.3";
            ((Button)ui.Window.FindName("AddMotorStep")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            ((Button)ui.Window.FindName("Preset23")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            ((TextBox)ui.Window.FindName("StepDuration")).Text="0.2";
            ((Button)ui.Window.FindName("AddMotorStep")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(((ListBox)ui.Window.FindName("MotorSteps")).Items.Count==2,"Actual sequence UI appends two configured steps");
            ((Button)ui.Window.FindName("MoveStepUp")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            ((Button)ui.Window.FindName("RunMotorSequence")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(ui.Robot.SequenceRunning,"Actual sequence run button starts prepared sequence");
            MotorEquals(ui.Robot.MotorCommands,new[]{0,0.4,0.4,0},"Sequence reorder changes execution order");
            Check(!((Button)ui.Window.FindName("Motor1Forward")).IsEnabled,"Manual hold disabled while automatic sequence owns motion");
            uiTime+=200; ui.Robot.Tick();
            MotorEquals(ui.Robot.MotorCommands,new[]{0.5,0,0,-0.6},"Actual UI sequence advances to captured earlier settings");
            ((Button)ui.Window.FindName("StopMotorSequence")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(!ui.Robot.SequenceRunning&&ui.Robot.MotorCommands.All(v=>v==0),"Actual sequence stop immediately clears all motor targets");
            ((Button)ui.Window.FindName("RunMotorSequence")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            ((Button)ui.Window.FindName("NavArms")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(!ui.Robot.SequenceRunning&&!ui.Robot.Armed,"Changing tab cancels automatic sequence and disarms");
            ui.PauseControl(); ui.Robot.Tick();
            Console.WriteLine("PASS: actual 4-motor hold/reverse/release/capture events, combination editing and timed sequence UI");
            ((Button)ui.Window.FindName("NavAi")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            const string fakeKey="sk-unit-test-placeholder-not-a-real-key-0000";
            ((PasswordBox)ui.Window.FindName("AiApiKey")).Password=fakeKey;
            ((CheckBox)ui.Window.FindName("AiEnabled")).IsChecked=true;
            ((Button)ui.Window.FindName("AiSave")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(((PasswordBox)ui.Window.FindName("AiApiKey")).Password.Length==0,"API key field clears immediately after USB submission");
            Check(((Dictionary<string,object>)uiWire.Sent.Last()["config"])["api_key"].Equals(fakeKey),"AI key is sent only in explicit provisioning command");
            ui.Robot.Tick();
            Check(!((TextBox)ui.Window.FindName("EventLog")).Text.Contains(fakeKey),"API key cannot enter the GUI event log");
            Check(!((TextBlock)ui.Window.FindName("Feedback")).Text.Contains(fakeKey),"API key cannot enter GUI feedback");
            Check(ui.Robot.AiSettings!=null && RobotController.Flag(ui.Robot.AiSettings,"key_configured"),"GUI reads only configured flag after save");
            ((TextBox)ui.Window.FindName("AiSsid")).Text="WALLE-test-network";
            ((PasswordBox)ui.Window.FindName("AiWifiPassword")).Password="test-password-0000";
            ((Button)ui.Window.FindName("AiSaveWifi")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Check(((PasswordBox)ui.Window.FindName("AiWifiPassword")).Password.Length==0,"Wi-Fi password clears after submission");
            var wifiPatch=(Dictionary<string,object>)uiWire.Sent.Last()["patch"];
            Check(!wifiPatch.ContainsKey("hardware_confirmed"),"AI network setup never bypasses physical motor confirmation");
            Console.WriteLine("PASS: actual AI UI saves via USB, clears password fields and keeps secrets out of logs");
            ui.Window.Close();
            Console.WriteLine("PASS: actual XAML loads, all tabs navigate and offline controls stay locked");
            Console.WriteLine("PASS: "+checks+" assertions; no physical port opened or actuator commands sent");
            return 0;
        } catch(Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
