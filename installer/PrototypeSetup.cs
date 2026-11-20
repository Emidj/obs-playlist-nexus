using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

[assembly: AssemblyTitle("OBS Playlist NEXUS Setup")]
[assembly: AssemblyProduct("OBS Playlist NEXUS")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

public class OwnedFile { public string path; public string sha256; }
public class InstallState { public string product="OBS Playlist NEXUS"; public string version="1.0.0"; public List<OwnedFile> files=new List<OwnedFile>(); }
public static class PrototypeSetup {
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    const string StatePath="obs-plugins/64bit/nexus-playlist-install.json";
    static bool IsObs(string path) {
        if(String.IsNullOrWhiteSpace(path)||path.IndexOfAny(Path.GetInvalidPathChars())>=0)return false;
        var exe=Path.Combine(path,"bin","64bit","obs64.exe");
        if(!File.Exists(exe))return false;
        var version=FileVersionInfo.GetVersionInfo(exe);
        return version.FileMajorPart==32||version.FileMajorPart==33;
    }
    static string SuggestedFolder() {
        var standard=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"obs-studio");
        if(IsObs(standard))return standard;
        var location=Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        for(var directory=location;directory!=null;directory=Path.GetDirectoryName(directory)) {
            if(IsObs(directory))return directory;
            var candidate=Path.Combine(directory,"work","frontend-33");if(IsObs(candidate))return candidate;
        }
        return standard;
    }
    static string Hash(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","");}
    static string Target(string root,string relative) {
        if(relative!=StatePath && (!relative.StartsWith("obs-plugins/64bit/",StringComparison.Ordinal)||relative.Substring(18).Contains("/")||!relative.EndsWith(".dll",StringComparison.Ordinal)))throw new Exception("Ruta de paquete no permitida: "+relative);
        var path=Path.GetFullPath(Path.Combine(root,relative.Replace('/',Path.DirectorySeparatorChar)));
        if(!path.StartsWith(root+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase))throw new Exception("Ruta fuera de destino");
        for(var dir=Path.GetDirectoryName(path);dir!=null;dir=Path.GetDirectoryName(dir))if(Directory.Exists(dir)&&(File.GetAttributes(dir)&FileAttributes.ReparsePoint)!=0)throw new Exception("No se admiten carpetas enlazadas");
        if(File.Exists(path)&&(File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new Exception("No se admiten archivos enlazados");
        return path;
    }
    static Dictionary<string,byte[]> Payload() {
        var result=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);
        using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("payload.zip"))
        using(var zip=new ZipArchive(stream,ZipArchiveMode.Read)) {
            foreach(var entry in zip.Entries)if(entry.FullName.StartsWith("obs-plugins/64bit/",StringComparison.Ordinal)&&entry.FullName.EndsWith(".dll",StringComparison.Ordinal))
                using(var input=entry.Open())using(var output=new MemoryStream()){input.CopyTo(output);result.Add(entry.FullName,output.ToArray());}
            var manifest=zip.GetEntry("manifest.json");
            using(var reader=new StreamReader(manifest.Open())) {
                var data=Json.DeserializeObject(reader.ReadToEnd()) as Dictionary<string,object>;
                var expected=(object[])data["files"];
                if(expected.Length!=result.Count||result.Count!=11)throw new Exception("Inventario de paquete incorrecto");
                foreach(Dictionary<string,object> item in expected){var name=(string)item["file"];if(!result.ContainsKey(name)||Hash(result[name])!=(string)item["sha256"])throw new Exception("Integridad incorrecta: "+name);}
            }
        }
        return result;
    }
    public static void Apply(string folder,bool uninstall) {
        var identity=Hash(Encoding.UTF8.GetBytes(Path.GetFullPath(folder).TrimEnd(Path.DirectorySeparatorChar).ToUpperInvariant()));
        using(var mutex=new Mutex(false,"Local\\NexusPlaylistSetup-"+identity)) {
            bool held=false;
            try {try{held=mutex.WaitOne(0);}catch(AbandonedMutexException){held=true;}
                if(!held)throw new Exception("Ya hay otra instalacion en esta carpeta");
                ApplyCore(folder,uninstall);
            }finally{if(held)mutex.ReleaseMutex();}
        }
    }
    static void ApplyCore(string folder,bool uninstall) {
        var root=Path.GetFullPath(folder).TrimEnd(Path.DirectorySeparatorChar);
        if(!Environment.Is64BitOperatingSystem)throw new Exception("Windows x64 requerido");
        if(!IsObs(root))throw new Exception("Selecciona la carpeta raíz de OBS Studio 32 o 33 x64.");
        if(Process.GetProcessesByName("obs64").Length!=0)throw new Exception("Cierra OBS antes de instalar o desinstalar");
        var stateFile=Target(root,StatePath);
        var previous=File.Exists(stateFile)?Json.Deserialize<InstallState>(File.ReadAllText(stateFile)):new InstallState();
        if(previous==null||previous.product!="OBS Playlist NEXUS"||previous.files==null)throw new Exception("Registro de instalacion no valido");
        var owned=new Dictionary<string,OwnedFile>(StringComparer.OrdinalIgnoreCase);
        foreach(var entry in previous.files){Target(root,entry.path);if(entry.path==StatePath)throw new Exception("Registro no valido");owned.Add(entry.path,entry);}
        // Preflight every managed file before writing anything.
        foreach(var entry in owned.Values) {
            var path=Target(root,entry.path);
            if(File.Exists(path)&&Hash(File.ReadAllBytes(path))!=entry.sha256)throw new Exception("Archivo modificado; se conserva sin cambios: "+entry.path);
        }
        var changes=new Dictionary<string,byte[]>(StringComparer.OrdinalIgnoreCase);
        var next=new InstallState();
        if(uninstall) {
            if(!File.Exists(stateFile))throw new Exception("No hay instalacion registrada en esta carpeta");
            foreach(var entry in owned.Values)changes[entry.path]=null;
            changes[StatePath]=null;
        }else {
            var payload=Payload();
            foreach(var item in payload) {
                var destination=Target(root,item.Key);
                if(File.Exists(destination)&&!owned.ContainsKey(item.Key)) {
                    if(Hash(File.ReadAllBytes(destination))!=Hash(item.Value))throw new Exception("DLL ajena diferente; no se sobrescribe: "+item.Key);
                    continue; // Shared, identical files are never claimed or removed.
                }
                changes[item.Key]=item.Value;
                next.files.Add(new OwnedFile{path=item.Key,sha256=Hash(item.Value)});
            }
            foreach(var entry in owned.Values)if(!payload.ContainsKey(entry.path))changes[entry.path]=null;
            changes[StatePath]=Encoding.UTF8.GetBytes(Json.Serialize(next));
        }
        // Snapshot and stage all writes first; restore snapshots on recoverable I/O failure.
        var backups=new Dictionary<string,byte[]>();
        var staged=new Dictionary<string,string>();
        var applied=new List<string>();
        try {
            foreach(var item in changes) {
                var path=Target(root,item.Key);Directory.CreateDirectory(Path.GetDirectoryName(path));
                backups[item.Key]=File.Exists(path)?File.ReadAllBytes(path):null;
                if(item.Value!=null){var temp=path+".nexus-"+Guid.NewGuid().ToString("N")+".tmp";staged[item.Key]=temp;File.WriteAllBytes(temp,item.Value);}
            }
            foreach(var item in changes.OrderBy(item=>item.Key==StatePath?1:0)) {
                var path=Target(root,item.Key);
                if(item.Value==null){if(File.Exists(path))File.Delete(path);}
                else if(File.Exists(path))File.Replace(staged[item.Key],path,null);
                else File.Move(staged[item.Key],path);
                applied.Add(item.Key);
            }
        }catch {
            var failures=new List<string>();
            foreach(var key in applied.AsEnumerable().Reverse())try {
                var path=Target(root,key);if(backups[key]==null){if(File.Exists(path))File.Delete(path);}else File.WriteAllBytes(path,backups[key]);
            }catch(Exception e){failures.Add(key+": "+e.Message);}
            if(failures.Count!=0)throw new Exception("Restauracion incompleta: "+String.Join("; ",failures));
            throw;
        }finally{foreach(var path in staged.Values)if(File.Exists(path))File.Delete(path);}
    }
    [STAThread] public static int Main(string[] args) {
        if(args.Length==2&&(args[0]=="--install"||args[0]=="--uninstall")) {
            try{Apply(args[1],args[0]=="--uninstall");return 0;}catch(Exception e){File.WriteAllText(Path.Combine(Path.GetTempPath(),"nexus-playlist-setup-error.txt"),e.ToString());return 1;}
        }
        Application.EnableVisualStyles();
        var background=Color.FromArgb(25,29,38);
        var muted=Color.FromArgb(165,174,193);
        using(var form=new Form {Text="OBS Playlist NEXUS 1.0",ClientSize=new Size(960,540),
            StartPosition=FormStartPosition.CenterScreen,FormBorderStyle=FormBorderStyle.FixedDialog,
            MaximizeBox=false,BackColor=background,ForeColor=Color.White,Font=new Font("Segoe UI",10),AutoScaleMode=AutoScaleMode.Dpi}) {
            using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("nexus-icon.ico"))
                if(stream!=null)using(var original=new Icon(stream))form.Icon=(Icon)original.Clone();
            var branding=new PictureBox {Left=0,Top=0,Width=360,Height=540,SizeMode=PictureBoxSizeMode.Zoom,TabStop=false,BackColor=Color.Black};
            using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("nexus-sidebar.png"))
                if(stream!=null)using(var original=Image.FromStream(stream))branding.Image=new Bitmap(original);
            form.FormClosed+=delegate {if(branding.Image!=null)branding.Image.Dispose();};

            var header=new Label {Text="OBS PLAYLIST NEXUS  /  1.0",Left=390,Top=32,Width=360,Height=26,ForeColor=muted};
            var language=new ComboBox {Left=774,Top=28,Width=150,DropDownStyle=ComboBoxStyle.DropDownList,
                ForeColor=SystemColors.WindowText,BackColor=SystemColors.Window};
            language.Items.AddRange(new object[]{"English","Español","Italiano","Français","Deutsch"});
            var title=new Label {Left=389,Top=83,Width=535,Height=90,Font=new Font("Segoe UI",24,FontStyle.Bold)};
            var help=new Label {Left=390,Top=183,Width=534,Height=70,ForeColor=muted};
            var destination=new Label {Left=390,Top=264,Width=530,Height=27,ForeColor=muted};
            var folder=new TextBox {Left=390,Top=298,Width=393,Text=SuggestedFolder(),BackColor=Color.FromArgb(33,38,49),ForeColor=Color.White,BorderStyle=BorderStyle.FixedSingle};
            var browse=ButtonAt(797,291,127,false);
            var status=new Label {Left=390,Top=338,Width=534,Height=64,ForeColor=muted};
            var divider=new Panel {Left=390,Top=428,Width=534,Height=1,BackColor=Color.FromArgb(55,62,78)};
            var remove=ButtonAt(390,455,145,false);
            var close=ButtonAt(549,455,135,false);
            var install=ButtonAt(698,455,226,true);
            form.AcceptButton=install;
            form.CancelButton=close;
            close.Click+=(sender,eventArgs)=>form.Close();

            Action updateStatus=delegate {
                bool valid=IsObs(folder.Text);
                install.Enabled=valid;
                remove.Enabled=valid&&File.Exists(Path.Combine(folder.Text,StatePath));
                status.Text=valid?LabelText(language.SelectedIndex,8):LabelText(language.SelectedIndex,9);
            };
            Action translate=delegate {
                int index=language.SelectedIndex;
                title.Text=LabelText(index,0);help.Text=LabelText(index,1);destination.Text=LabelText(index,2);
                browse.Text=LabelText(index,3);remove.Text=LabelText(index,4);close.Text=LabelText(index,5);
                install.Text=LabelText(index,6);updateStatus();
            };
            language.SelectedIndexChanged+=(sender,eventArgs)=>translate();
            folder.TextChanged+=(sender,eventArgs)=>updateStatus();
            browse.Click+=(sender,eventArgs)=>{
                using(var picker=new FolderBrowserDialog {Description=destination.Text,ShowNewFolderButton=false}){
                    if(Directory.Exists(folder.Text))picker.SelectedPath=folder.Text;
                    if(picker.ShowDialog(form)==DialogResult.OK)folder.Text=picker.SelectedPath;
                }
            };
            Action<bool> execute=delegate(bool uninstall){
                try{
                    if(uninstall&&MessageBox.Show(form,LabelText(language.SelectedIndex,10),form.Text,
                        MessageBoxButtons.YesNo,MessageBoxIcon.Question)!=DialogResult.Yes)return;
                    try {Apply(folder.Text,uninstall);}
                    catch(UnauthorizedAccessException){
                        string path=Path.GetFullPath(folder.Text).TrimEnd(Path.DirectorySeparatorChar);
                        var start=new ProcessStartInfo(Assembly.GetExecutingAssembly().Location,
                            (uninstall?"--uninstall":"--install")+" \""+path+"\""){UseShellExecute=true,Verb="runas",WindowStyle=ProcessWindowStyle.Hidden};
                        using(var elevated=Process.Start(start)){
                            elevated.WaitForExit();
                            if(elevated.ExitCode!=0)throw new IOException(LabelText(language.SelectedIndex,11));
                        }
                    }
                    MessageBox.Show(form,LabelText(language.SelectedIndex,uninstall?12:7),form.Text);
                }catch(Exception e){MessageBox.Show(form,e.Message,form.Text,MessageBoxButtons.OK,MessageBoxIcon.Error);}
                finally{updateStatus();}
            };
            install.Click+=(sender,eventArgs)=>execute(false);
            remove.Click+=(sender,eventArgs)=>execute(true);
            string culture=System.Globalization.CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
            language.SelectedIndex=Math.Max(0,Array.IndexOf(new[]{"en","es","it","fr","de"},culture));
            form.Controls.AddRange(new Control[]{branding,header,language,title,help,destination,folder,browse,status,divider,remove,close,install});
            if(args.Length==2&&args[0]=="--render"){
                form.Show();
                Application.DoEvents();
                using(var shot=new Bitmap(form.ClientSize.Width,form.ClientSize.Height)){
                    form.DrawToBitmap(shot,form.ClientRectangle);
                    shot.Save(args[1],System.Drawing.Imaging.ImageFormat.Png);
                }
                form.Close();
                return 0;
            }
            Application.Run(form);
            return 0;
        }
    }
    static Button ButtonAt(int x,int y,int width,bool primary){
        var button=new Button{Left=x,Top=y,Width=width,Height=44,FlatStyle=FlatStyle.Flat,
            BackColor=primary?Color.FromArgb(109,53,199):Color.FromArgb(38,44,58),ForeColor=Color.White};
        button.FlatAppearance.BorderColor=Color.FromArgb(65,73,93);
        return button;
    }
    static string LabelText(int language,int key){
        return SetupLabels[Math.Max(0,language)][key];
    }
    static readonly string[][] SetupLabels={
        new[]{"Install OBS Playlist NEXUS","Install, update or remove the video playlist plugin for OBS Studio 32 or 33 on Windows x64.","OBS Studio folder","Browse…","Uninstall","Close","Install / update","Installation complete. Add the OBS Playlist NEXUS source in OBS.","Compatible OBS installation found. Close OBS before continuing.","Choose the OBS folder that contains bin\\64bit\\obs64.exe.","Uninstall OBS Playlist NEXUS from this folder?","Elevated operation failed. Check the setup error log.","OBS Playlist NEXUS removed."},
        new[]{"Instalar OBS Playlist NEXUS","Instala, actualiza o retira el plugin de listas de vídeo para OBS Studio 32 o 33 en Windows x64.","Carpeta de OBS Studio","Examinar…","Desinstalar","Cerrar","Instalar / actualizar","Instalación completada. Añade la fuente OBS Playlist NEXUS en OBS.","Instalación compatible detectada. Cierra OBS antes de continuar.","Selecciona la carpeta de OBS que contiene bin\\64bit\\obs64.exe.","¿Desinstalar OBS Playlist NEXUS de esta carpeta?","La operación con permisos de administrador falló. Consulta el registro del instalador.","OBS Playlist NEXUS desinstalado."},
        new[]{"Installa OBS Playlist NEXUS","Installa, aggiorna o rimuovi il plugin delle playlist video per OBS Studio 32 o 33 su Windows x64.","Cartella di OBS Studio","Sfoglia…","Disinstalla","Chiudi","Installa / aggiorna","Installazione completata. Aggiungi la sorgente OBS Playlist NEXUS in OBS.","Installazione compatibile rilevata. Chiudi OBS prima di continuare.","Scegli la cartella di OBS che contiene bin\\64bit\\obs64.exe.","Disinstallare OBS Playlist NEXUS da questa cartella?","Operazione con privilegi elevati non riuscita. Consulta il registro.","OBS Playlist NEXUS disinstallato."},
        new[]{"Installer OBS Playlist NEXUS","Installez, mettez à jour ou retirez le module de listes vidéo pour OBS Studio 32 ou 33 sous Windows x64.","Dossier d’OBS Studio","Parcourir…","Désinstaller","Fermer","Installer / mettre à jour","Installation terminée. Ajoutez la source OBS Playlist NEXUS dans OBS.","Installation compatible détectée. Fermez OBS avant de continuer.","Choisissez le dossier OBS qui contient bin\\64bit\\obs64.exe.","Désinstaller OBS Playlist NEXUS de ce dossier ?","L’opération avec élévation a échoué. Consultez le journal d’installation.","OBS Playlist NEXUS désinstallé."},
        new[]{"OBS Playlist NEXUS installieren","Installiere, aktualisiere oder entferne das Video-Playlist-Plugin für OBS Studio 32 oder 33 unter Windows x64.","OBS-Studio-Ordner","Durchsuchen…","Deinstallieren","Schließen","Installieren / aktualisieren","Installation abgeschlossen. Füge in OBS die Quelle OBS Playlist NEXUS hinzu.","Kompatible Installation erkannt. Schließe OBS vor dem Fortfahren.","Wähle den OBS-Ordner mit bin\\64bit\\obs64.exe.","OBS Playlist NEXUS aus diesem Ordner entfernen?","Vorgang mit Administratorrechten fehlgeschlagen. Prüfe das Installationsprotokoll.","OBS Playlist NEXUS wurde entfernt."}
    };
}
