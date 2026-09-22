"""Janela de controle do servidor local LUDO."""
import json
import socket
import threading
import tkinter as tk
from pathlib import Path
from tkinter import ttk, filedialog, messagebox
from ludo_server import Store, Server

class App:
    def __init__(self, root):
        self.root=root;self.server=None;self.thread=None
        self.settings=Path.home()/'.ludo-server-settings.json'
        try: config=json.loads(self.settings.read_text(encoding='utf-8'))
        except (OSError,ValueError): config={}
        root.title('LUDO Servidor');root.geometry('660x560');root.minsize(600,540)
        panel=ttk.Frame(root,padding=18);panel.pack(fill='both',expand=True)
        ttk.Label(panel,text='LUDO Servidor',font=('Segoe UI',20,'bold')).pack(anchor='w')
        ttk.Label(panel,text='Servidor local para LAN/VPN. Para Internet, use o serviço cloud com HTTPS.').pack(anchor='w',pady=(0,16))
        ttk.Label(panel,text='Pasta dos projetos e histórico').pack(anchor='w')
        row=ttk.Frame(panel);row.pack(fill='x',pady=5)
        self.folder=tk.StringVar(value=config.get('folder',str(Path.home()/'LudoServidor')))
        self.folder_entry=ttk.Entry(row,textvariable=self.folder);self.folder_entry.pack(side='left',fill='x',expand=True)
        self.choose_button=ttk.Button(row,text='Escolher…',command=self.choose);self.choose_button.pack(side='left',padx=6)
        row=ttk.Frame(panel);row.pack(fill='x',pady=8)
        ttk.Label(row,text='Porta').pack(side='left');self.port=tk.StringVar(value=str(config.get('port',8787)))
        self.port_entry=ttk.Entry(row,textvariable=self.port,width=8);self.port_entry.pack(side='left',padx=8)
        self.lan=tk.BooleanVar(value=False)
        self.lan_box=ttk.Checkbutton(row,text='Permitir acesso pela rede local / VPN',variable=self.lan);self.lan_box.pack(side='left')
        self.status=tk.StringVar(value='Servidor parado. Crie o administrador antes de iniciar.')
        ttk.Label(panel,textvariable=self.status,wraplength=610).pack(anchor='w',pady=8)
        row=ttk.Frame(panel);row.pack(fill='x',pady=5)
        self.start_button=ttk.Button(row,text='Iniciar servidor',command=self.toggle);self.start_button.pack(side='left')
        ttk.Button(row,text='Fazer backup',command=self.backup).pack(side='left',padx=8)
        box=ttk.LabelFrame(panel,text='Equipe',padding=10);box.pack(fill='both',expand=True,pady=12)
        self.people=tk.Listbox(box,height=5);self.people.pack(fill='both',expand=True)
        row=ttk.Frame(box);row.pack(fill='x',pady=(8,0))
        ttk.Button(row,text='Adicionar pessoa…',command=self.add_user).pack(side='left')
        ttk.Button(row,text='Atualizar lista',command=self.refresh).pack(side='left',padx=8)
        ttk.Button(row,text='Remover pessoa',command=self.remove_user).pack(side='left')
        ttk.Label(panel,text='Esta janela é para LAN/VPN. Não exponha esta porta diretamente na Internet.\nPara cloud, use o serviço Linux atrás de HTTPS. Copie os backups para outro dispositivo.',wraplength=610).pack(anchor='w')
        root.protocol('WM_DELETE_WINDOW',self.close)
        self.refresh();root.after(3600000,self.auto_backup)

    def store(self):
        folder=self.folder.get().strip()
        if not folder: raise ValueError('Escolha a pasta do servidor.')
        return Store(folder)
    def choose(self):
        path=filedialog.askdirectory(title='Pasta do servidor')
        if path:self.folder.set(path);self.refresh()
    def refresh(self):
        try:
            users=self.store().users();self.user_names=[u['name'] for u in users];self.people.delete(0,'end')
            labels={'admin':'Administrador','editor':'Editor','viewer':'Visualização'}
            for user in users:self.people.insert('end',user['name']+' — '+labels[user['role']])
        except Exception as error:messagebox.showerror('Equipe',str(error))
    def remove_user(self):
        selection=self.people.curselection()
        if not selection:return
        name=self.user_names[selection[0]]
        if not messagebox.askyesno('Remover acesso',f'Remover {name}? As sessões e reservas serão encerradas.'):return
        try:self.store().remove_user(name);self.refresh()
        except Exception as error:messagebox.showerror('Equipe',str(error))
    def add_user(self):
        dlg=tk.Toplevel(self.root);dlg.title('Adicionar pessoa');dlg.transient(self.root);dlg.grab_set()
        frame=ttk.Frame(dlg,padding=20);frame.pack(fill='both',expand=True)
        name=tk.StringVar();password=tk.StringVar();confirm=tk.StringVar();role=tk.StringVar(value='Editor')
        for label,var,secret in [('Nome de acesso',name,False),('Senha (mínimo 10 caracteres)',password,True),('Confirmar senha',confirm,True)]:
            ttk.Label(frame,text=label).pack(anchor='w');ttk.Entry(frame,textvariable=var,show='*' if secret else '',width=38).pack(fill='x',pady=(0,8))
        ttk.Label(frame,text='Permissão em todos os projetos deste servidor').pack(anchor='w')
        ttk.Combobox(frame,textvariable=role,values=['Administrador','Editor','Visualização'],state='readonly').pack(fill='x',pady=6)
        def save():
            try:
                if password.get()!=confirm.get():raise ValueError('As senhas não coincidem.')
                roles={'Administrador':'admin','Editor':'editor','Visualização':'viewer'}
                self.store().add_user(name.get(),password.get(),roles[role.get()])
                password.set('');confirm.set('');dlg.destroy();self.refresh()
            except Exception as error:messagebox.showerror('Não foi possível adicionar',str(error),parent=dlg)
        ttk.Button(frame,text='Adicionar',command=save).pack(anchor='e',pady=8)
    def toggle(self):
        if self.server:
            self.stop();return
        try:
            store=self.store()
            if not any(u['role']=='admin' for u in store.users()):raise ValueError('Adicione uma pessoa com permissão de Administrador.')
            port=int(self.port.get())
            if not 1024<=port<=65535:raise ValueError('Use uma porta entre 1024 e 65535.')
            self.server=Server(('0.0.0.0' if self.lan.get() else '127.0.0.1',port),store)
            self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
            self.settings.write_text(json.dumps({'folder':self.folder.get(),'port':port}),encoding='utf-8')
            try: addresses=socket.gethostbyname_ex(socket.gethostname())[2]
            except OSError: addresses=[]
            urls=['http://127.0.0.1:'+str(port)]
            if self.lan.get():urls += ['http://'+ip+':'+str(port) for ip in addresses if not ip.startswith('127.')]
            self.status.set('Servidor ativo. Endereços: '+ ' | '.join(urls))
            self.start_button.config(text='Parar servidor')
            for control in (self.folder_entry,self.choose_button,self.port_entry,self.lan_box):control.config(state='disabled')
        except Exception as error:
            if self.server:self.stop()
            messagebox.showerror('Servidor',str(error))
    def stop(self):
        if self.server:
            self.server.shutdown();self.server.server_close();self.server=None
        self.status.set('Servidor parado. As cópias locais continuam disponíveis.')
        self.start_button.config(text='Iniciar servidor')
        for control in (self.folder_entry,self.choose_button,self.port_entry,self.lan_box):control.config(state='normal')
    def backup(self):
        try:messagebox.showinfo('Backup concluído',str(self.store().backup())+'\n\nCopie este arquivo para outro dispositivo.')
        except Exception as error:messagebox.showerror('Backup',str(error))
    def auto_backup(self):
        if self.server:
            try:self.server.store.backup()
            except Exception as error:self.status.set('Falha no backup: '+str(error))
        self.root.after(3600000,self.auto_backup)
    def close(self):
        if self.server and not messagebox.askyesno('Parar servidor?','A sincronização da equipe ficará indisponível. Fechar?'):return
        self.stop();self.root.destroy()

if __name__=='__main__':
    root=tk.Tk();App(root);root.mainloop()
