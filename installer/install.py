"""Offline Windows installer. No telemetry, network access or automatic flashing."""
import hashlib
import io
import json
from pathlib import Path
import queue
import sys
import threading

IMAGE = 'pocket-studio-v0.25-cardputer-adv-full.bin'
SHA256 = '41f1af21f7ae9ba7128a7cf557b862f48e4888e2905440eef99ac9a74b6e0b7d'

def image_path():
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent)) / IMAGE

def verify_image():
    path = image_path()
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError('固件文件缺失或校验失败，请重新下载完整安装包。')
    return path

def main():
    import esptool
    from serial.tools import list_ports
    if '--self-test' in sys.argv:
        verify_image()
        from esptool.targets.esp32s3 import ESP32S3ROM
        from esptool.loader import StubFlasher
        StubFlasher(ESP32S3ROM)
        import tkinter as tk
        test_root = tk.Tk(); test_root.withdraw(); test_root.update_idletasks(); test_root.destroy()
        result = {'image_sha256': SHA256, 'firmware_integrity': 'PASS', 'esptool': esptool.__version__, 'esp32s3_stub': 'PASS', 'tk_runtime': 'PASS', 'device_access': False}
        if '--report' in sys.argv:
            Path(sys.argv[sys.argv.index('--report')+1]).write_text(json.dumps(result, indent=2), encoding='utf-8')
        return

    import tkinter as tk
    from tkinter import ttk, messagebox
    root = tk.Tk(); root.title('Pocket Studio v0.25 · Cardputer ADV')
    root.geometry('740x570'); root.minsize(680,520)
    frame = ttk.Frame(root, padding=22); frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='Pocket Studio', font=('Microsoft YaHei UI', 25, 'bold')).pack(anchor='w')
    ttk.Label(frame, text='v0.25  /  Cardputer ADV  /  Windows x64 离线安装').pack(anchor='w', pady=(0,16))
    ttk.Label(frame, text='1. 用可传数据的 USB 线连接 Cardputer ADV。\n2. 选择它的串口，确认备份后点击“安装固件”。', justify='left').pack(anchor='w')
    row = ttk.Frame(frame); row.pack(fill='x', pady=14)
    port = tk.StringVar(); devices = ttk.Combobox(row, textvariable=port, state='readonly', width=54); devices.pack(side='left', fill='x', expand=True)
    values = {}; busy = False; messages = queue.Queue()
    def refresh():
        nonlocal values
        values = {f'{p.device}  {p.description}': p.device for p in list_ports.comports()}
        devices['values'] = list(values)
        preferred = next((label for label in values if '303A' in next((p.hwid for p in list_ports.comports() if p.device==values[label]), '')), None)
        if preferred: port.set(preferred)
        elif len(values)==1: port.set(next(iter(values)))
        else: port.set('')
    ttk.Button(row, text='刷新串口', command=refresh).pack(side='right', padx=(8,0))
    consent = tk.BooleanVar(value=False)
    ttk.Checkbutton(frame, text='已备份：本次安装会重写固件和内部存档；不会写入 SD 卡。', variable=consent).pack(anchor='w', pady=(0,8))
    status = tk.StringVar(value='就绪。请先选择 Cardputer ADV 的串口。')
    ttk.Label(frame, textvariable=status, wraplength=650).pack(anchor='w', pady=5)
    log = tk.Text(frame, height=15, font=('Consolas',9), wrap='word'); log.pack(fill='both', expand=True, pady=8)
    class LogStream(io.TextIOBase):
        def write(self, text): messages.put(('log', str(text))); return len(text)
        def flush(self): pass
        def isatty(self): return False
    def worker(target):
        import contextlib
        try:
            image = verify_image()
            with contextlib.redirect_stdout(LogStream()), contextlib.redirect_stderr(LogStream()):
                try:
                    esptool.main(['--chip','esp32s3','--port',target,'--baud','460800','write-flash','--flash-size','8MB','0x0',str(image)])
                except SystemExit as e:
                    if e.code not in (None,0): raise RuntimeError(f'烧录失败，退出码 {e.code}')
            messages.put(('done','安装完成，烧录校验通过。设备将自动重启。'))
        except Exception as e:
            messages.put(('error',str(e)))
    def install():
        nonlocal busy
        if busy: return
        if port.get() not in values: messagebox.showinfo('选择串口','请先选择 Cardputer ADV 的串口。'); return
        if not consent.get(): messagebox.showinfo('备份确认','请先备份并勾选上方确认框。'); return
        busy=True; install_button['state']='disabled'; status.set('正在安装，请保持连接，不要关闭窗口……')
        threading.Thread(target=worker, args=(values[port.get()],), daemon=True).start()
    install_button = ttk.Button(frame, text='安装固件', command=install); install_button.pack(anchor='e', pady=(6,0))
    def poll():
        nonlocal busy
        while True:
            try: kind,text=messages.get_nowait()
            except queue.Empty: break
            if kind=='log': log.insert('end',text); log.see('end')
            else:
                busy=False; install_button['state']='normal'; status.set(text)
                if kind=='error': messagebox.showerror('安装未完成',text+'\n如无法连接，请按住 G0 键并按复位进入下载模式，刷新串口后重试。')
        root.after(100,poll)
    def close():
        if busy: messagebox.showinfo('正在烧录','请等待烧录结束。'); return
        root.destroy()
    root.protocol('WM_DELETE_WINDOW',close); refresh(); poll(); root.mainloop()

if __name__ == '__main__':
    main()
