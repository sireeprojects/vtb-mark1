#!/usr/bin/env python3
import subprocess
import sys
import argparse
import termios
import os
import signal

def run_frontend(nump, numq):
    executable = "./frontend"
    base_mac_prefix = "00:11:22:33:44:5"
    socket_path = "/tmp/vhost-user.sock"
    
    if not os.path.isfile(executable):
        print(f"Error: {executable} not found.")
        sys.exit(1)

    cmd = [executable, "-l 3-4", "--file-prefix=virtio_frontend",
                       "--no-telemetry", "--log-level=lib.eal:3"]
    for i in range(nump):
        vdev_str = f"net_virtio_user{i},path={socket_path},server=0,queues={numq},mac={base_mac_prefix}{i}"
        cmd.append(f"--vdev={vdev_str}")

    fd = sys.stdin.fileno()
    is_terminal = os.isatty(fd)
    if is_terminal:
        old_settings = termios.tcgetattr(fd)
        new_settings = termios.tcgetattr(fd)
        new_settings[3] = new_settings[3] & ~termios.ECHOCTL

    # print("Constructed Command:")
    # print(" \\\n    ".join(cmd))
    # print("-" * 50)

#    try:
#        # We use subprocess.run, but we catch the KeyboardInterrupt 
#        # specifically to avoid the "Traceback" spam.
#        subprocess.run(cmd, check=True)
#    except KeyboardInterrupt:
#        # This catch happens when you hit Ctrl+C
#        print("\n[Terminated by User]")
#    except subprocess.CalledProcessError as e:
#        print(f"\nError: Frontend exited with code {e.returncode}")
#    except Exception as e:
#        print(f"\nUnexpected error: {e}")

    # --- CRITICAL CHANGE START ---
    # We tell Python to ignore SIGINT (Ctrl+C). 
    # The C++ child process will still receive it.
    handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
    
    process = None
    try:
        if is_terminal:
            termios.tcsetattr(fd, termios.TCSADRAIN, new_settings)
        
        # Start the process
        process = subprocess.Popen(cmd)
        
        # Wait for the child to finish. 
        # Even if you hit Ctrl+C, this 'wait' continues until the child exits.
        process.wait()
    
    except Exception as e:
        print(f"\nUnexpected error: {e}")
    finally:
        # Restore the original signal handler
        signal.signal(signal.SIGINT, handler)
        
        if is_terminal:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
            
        print("\r<< Python script exiting after child cleanup >>")
    # --- CRITICAL CHANGE END ---

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("nump", type=int)
    parser.add_argument("numq", type=int)
    args = parser.parse_args()
    
    run_frontend(args.nump, args.numq)
    sys.exit(0)
