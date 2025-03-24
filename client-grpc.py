import tkinter as tk
from tkinter import messagebox
import threading
import subprocess
import queue
import json
import sys

DELIMITER = "\x1F"  # ASCII Unit Separator

class ChatClientGUI:
    def __init__(self, root, server_ip, port):
        self.root = root
        self.root.title("Chat Client")
        self.server_ip = server_ip
        self.port = port

        print("[DEBUG] Starting ChatClientGUI with server_ip:", server_ip, "port:", port)

        self.queue = queue.Queue()
        self.client_process = subprocess.Popen(
            ["./client", self.server_ip, str(self.port)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
        print("[DEBUG] Client process started with PID:", self.client_process.pid)
        self.root.after(1000, self.monitor_client)

        self.messages = []          # List to store displayed messages
        self.pending_messages = []  # List to store sent messages awaiting ACK
        self.root.after(100, self.process_queue)
        self.login_window()

        self.reader_thread = threading.Thread(target=self.read_from_client, daemon=True)
        self.reader_thread.start()

    def monitor_client(self):
        retcode = self.client_process.poll()
        if retcode is not None:
            print("[DEBUG] Client process terminated with return code:", retcode)
            # Optionally, notify the user or restart the client.
        else:
            self.root.after(1000, self.monitor_client)

    def login_window(self):
        for widget in self.root.winfo_children():
            widget.destroy()
        self.login_frame = tk.Frame(self.root)
        self.login_frame.pack(pady=20)
        tk.Label(self.login_frame, text="Username:").grid(row=0, column=0)
        self.username_entry = tk.Entry(self.login_frame)
        self.username_entry.grid(row=0, column=1)
        tk.Label(self.login_frame, text="Password:").grid(row=1, column=0)
        self.password_entry = tk.Entry(self.login_frame, show="*")
        self.password_entry.grid(row=1, column=1)
        self.auth_mode = tk.StringVar(value="L")
        tk.Radiobutton(self.login_frame, text="Login", variable=self.auth_mode, value="L").grid(row=2, column=0)
        tk.Radiobutton(self.login_frame, text="Register", variable=self.auth_mode, value="R").grid(row=2, column=1)
        self.submit_button = tk.Button(self.login_frame, text="Proceed", command=self.send_authentication)
        self.submit_button.grid(row=3, column=0, columnspan=2, pady=10)
        print("[DEBUG] Login window displayed.")

    def send_authentication(self):
        username = self.username_entry.get()
        password = self.password_entry.get()
        mode = self.auth_mode.get()
        print("[DEBUG] Sending authentication command:", mode, username)
        if username and password:
            command = f"{mode}{DELIMITER}{username}{DELIMITER}{password}"
            # Set current_user immediately; if authentication fails, the GUI won't progress.
            self.current_user = username
            self.send_to_client(command)
        else:
            messagebox.showerror("Error", "Username and Password required!")

    def open_main_window(self):
        self.login_frame.destroy()
        # Left pane: user list and offline message retrieval.
        self.left_frame = tk.Frame(self.root, width=200)
        self.left_frame.pack(side=tk.LEFT, padx=10, fill=tk.Y)
        tk.Label(self.left_frame, text="Search Users:").pack()
        self.search_entry = tk.Entry(self.left_frame)
        self.search_entry.pack()
        self.search_button = tk.Button(self.left_frame, text="Search", command=self.search_users)
        self.search_button.pack(pady=(0,10))
        tk.Label(self.left_frame, text="User List:").pack()
        self.user_listbox = tk.Listbox(self.left_frame, height=10)
        self.user_listbox.pack(fill=tk.BOTH, expand=True, pady=(0,10))
        # New: Offline message retrieval controls.
        tk.Label(self.left_frame, text="Retrieve Offline Messages:").pack(pady=(10,0))
        self.retrieve_spinbox = tk.Spinbox(self.left_frame, from_=1, to=100, width=5)
        self.retrieve_spinbox.pack()
        self.retrieve_button = tk.Button(self.left_frame, text="Retrieve", command=self.retrieve_messages)
        self.retrieve_button.pack(pady=(5,10))

        # Center pane: chat messages.
        self.center_frame = tk.Frame(self.root)
        self.center_frame.pack(side=tk.LEFT, padx=10, fill=tk.BOTH, expand=True)
        chat_list_frame = tk.Frame(self.center_frame)
        chat_list_frame.pack(fill=tk.BOTH, expand=True)
        tk.Label(chat_list_frame, text="Chat Messages:").pack(anchor="w")
        self.message_listbox = tk.Listbox(chat_list_frame, height=15, font=("Courier", 10), width=80)
        self.message_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.message_scrollbar = tk.Scrollbar(chat_list_frame)
        self.message_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.message_listbox.config(yscrollcommand=self.message_scrollbar.set)
        self.message_scrollbar.config(command=self.message_listbox.yview)

        # Bottom (send message) controls.
        send_frame = tk.Frame(self.center_frame)
        send_frame.pack(fill=tk.X, pady=10)
        tk.Label(send_frame, text="Recipient:").grid(row=0, column=0, sticky="w", padx=5)
        self.recipient_entry = tk.Entry(send_frame, width=50)
        self.recipient_entry.grid(row=0, column=1, sticky="ew", padx=5)
        tk.Label(send_frame, text="Message:").grid(row=1, column=0, sticky="w", padx=5, pady=(5,0))
        self.message_entry = tk.Entry(send_frame, width=50)
        self.message_entry.grid(row=1, column=1, sticky="ew", padx=5, pady=(5,0))
        self.send_button = tk.Button(send_frame, text="Send", command=self.send_message)
        self.send_button.grid(row=2, column=0, columnspan=2, pady=(10,0))
        send_frame.columnconfigure(1, weight=1)

        # Right pane: deletion and account controls.
        self.right_frame = tk.Frame(self.root, width=200)
        self.right_frame.pack(side=tk.RIGHT, padx=10, fill=tk.Y)
        top_right_frame = tk.Frame(self.right_frame)
        top_right_frame.pack(side=tk.TOP, fill=tk.X, pady=(10,10))
        tk.Label(top_right_frame, text="Delete Selected Message:").pack(pady=(0,5))
        self.delete_button = tk.Button(top_right_frame, text="Delete", command=self.delete_message)
        self.delete_button.pack()
        bottom_right_frame = tk.Frame(self.right_frame)
        bottom_right_frame.pack(side=tk.BOTTOM, fill=tk.X, pady=10)
        self.account_label = tk.Label(bottom_right_frame, text=f"Logged in as: {self.current_user}")
        self.account_label.pack(pady=(0,5))
        self.logout_button = tk.Button(bottom_right_frame, text="Log Out", command=self.logout)
        self.logout_button.pack(side=tk.LEFT, padx=5)
        self.delete_account_button = tk.Button(bottom_right_frame, text="Delete Account", command=self.delete_account)
        self.delete_account_button.pack(side=tk.LEFT, padx=5)
        print("[DEBUG] Main window opened for user:", self.current_user)

    def search_users(self):
        search_term = self.search_entry.get()
        print("[DEBUG] Searching users with term:", search_term)
        if search_term:
            command = f"l{DELIMITER}{search_term}"
            self.send_to_client(command)

    def retrieve_messages(self):
        try:
            num = int(self.retrieve_spinbox.get())
        except ValueError:
            messagebox.showerror("Error", "Please enter a valid number.")
            return
        command = f"r{DELIMITER}{self.current_user}{DELIMITER}{num}"
        print("[DEBUG] Sending retrieve offline messages command:", command)
        self.send_to_client(command)

    def send_message(self):
        sender = self.current_user
        recipient = self.recipient_entry.get()
        message = self.message_entry.get()
        print("[DEBUG] Sending message from", sender, "to", recipient, ":", message)
        if recipient and message:
            # Queue the outgoing message locally.
            pending = {"sender": sender, "recipient": recipient, "content": message}
            self.pending_messages.append(pending)
            command = f"s{DELIMITER}{sender}{DELIMITER}{recipient}{DELIMITER}{message}"
            self.send_to_client(command)
            self.message_entry.delete(0, tk.END)
        else:
            messagebox.showerror("Error", "Recipient and Message required!")

    def delete_message(self):
        selection = self.message_listbox.curselection()
        if not selection:
            messagebox.showerror("Error", "Please select a message to delete.")
            return
        index = selection[0]
        message_obj = self.messages[index]
        message_id = message_obj.get("message_id")
        print("[DEBUG] Deleting message with id:", message_id)
        command = f"d{DELIMITER}{self.current_user}{DELIMITER}{message_id}"
        self.send_to_client(command)
        self.message_listbox.delete(index)
        del self.messages[index]

    def logout(self):
        print("[DEBUG] Logging out user:", self.current_user)
        self.send_to_client("q")
        self.client_process.terminate()
        for widget in self.root.winfo_children():
            widget.destroy()
        self.__init__(self.root, self.server_ip, self.port)

    def delete_account(self):
        command = f"D{DELIMITER}{self.current_user}"
        print("[DEBUG] Sending delete account command for user:", self.current_user)
        self.send_to_client(command)

    def read_from_client(self):
        while True:
            output = self.client_process.stdout.readline()
            if output:
                print("[DEBUG] Read from client process:", output.strip())
                self.queue.put(output.strip())
                self.root.after(100, self.process_queue)
            else:
                break

    def process_queue(self):
        while not self.queue.empty():
            message = self.queue.get()
            print("[DEBUG] Message buffered:", message)
            # If the message doesn't start with '{', assume it's a debug/system message.
            if not message.startswith("{"):
                print("[SYSTEM]", message)
                continue
            try:
                data = json.loads(message)
            except Exception as e:
                print("[DEBUG] Error parsing JSON:", e)
                continue

            if data.get("type") == "confirmation":
                # When receiving an ACK, pop the earliest pending message,
                # combine it with the ack's message_id, and display as a sent message.
                if self.pending_messages:
                    pending = self.pending_messages.pop(0)
                    confirmed = {
                        "sender": self.current_user,
                        "recipient": pending["recipient"],
                        "content": pending["content"],
                        "message_id": data.get("message_id")
                    }
                    self.messages.append(confirmed)
                    display_str = f"[{confirmed.get('sender')}][{confirmed.get('message_id')}]: {confirmed.get('content')}"
                    display_str = display_str.rjust(80)
                    self.message_listbox.insert(tk.END, display_str)
                    print("[DEBUG] Displayed sent message with id:", confirmed.get("message_id"))
                else:
                    # If no pending message exists, fallback to displaying the ack.
                    self.messages.append(data)
                    display_str = f"[{data.get('sender')}][{data.get('message_id')}]: {data.get('content')}"
                    display_str = display_str.rjust(80)
                    self.message_listbox.insert(tk.END, display_str)
                    print("[DEBUG] Received confirmation (fallback) for message id:", data.get("message_id"))
            elif data.get("type") == "validation":
                print("[DEBUG] Received validation message:", data)
                if "successful" in data.get("message", "").lower():
                    self.current_user = self.username_entry.get()
                    self.open_main_window()
            elif data.get("type") in ("chat", "history"):
                self.messages.append(data)
                display_str = f"[{data.get('sender')}][{data.get('message_id')}]: {data.get('content')}"
                if data.get("sender") == self.current_user:
                    display_str = display_str.rjust(80)
                self.message_listbox.insert(tk.END, display_str)
                print("[DEBUG] Received chat/history message:", data)
            elif data.get("type") == "user_list":
                user_list_str = data.get("message", "").strip()
                if user_list_str:
                    usernames = [name.strip() for name in user_list_str.split(",") if name.strip()]
                    if hasattr(self, "user_listbox"):
                        self.user_listbox.delete(0, tk.END)
                        for username in usernames:
                            self.user_listbox.insert(tk.END, username)
                    print("[DEBUG] Updated user list:", usernames)
            elif data.get("type") == "delete":
                del_id = data.get("message_id")
                for idx, msg in enumerate(self.messages):
                    if msg.get("message_id") == del_id:
                        self.message_listbox.delete(idx)
                        del self.messages[idx]
                        print(f"[DEBUG] Deleted message {del_id} from listbox.")
                        break
            else:
                print("[SYSTEM JSON]", data)

    def send_to_client(self, message):
        try:
            print("[DEBUG] Sending to client process:", message)
            self.client_process.stdin.write(message + "\n")
            self.client_process.stdin.flush()
        except BrokenPipeError:
            messagebox.showerror("Error", "Pipe Fucked - Client connection lost.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: python3 {sys.argv[0]} <server_ip> <port>")
        sys.exit(1)
    server_ip = sys.argv[1]
    port = sys.argv[2]
    root = tk.Tk()
    app = ChatClientGUI(root, server_ip, port)
    root.mainloop()
