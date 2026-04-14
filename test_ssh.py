#!/usr/bin/env python3
import subprocess

def ssh_command(host, user, password, command, timeout=30):
    cmd = ['sshpass', '-p', password, 'ssh', '-o', 'StrictHostKeyChecking=no', '-o', f'ConnectTimeout={timeout}', f'{user}@{host}', command]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return result.stdout, result.stderr, result.returncode
    except Exception as e:
        return '', str(e), 1

if __name__ == '__main__':
    out, err, code = ssh_command('192.168.0.34', 'mobile', '001314', 'echo test')
    print('OUT:', out)
    print('ERR:', err)
    print('CODE:', code)
