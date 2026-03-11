import socket, time
print("Connecting...")
s = socket.socket()
s.connect(('127.0.0.1', 8080))
print("Connected. Sleeping 5 seconds...")
time.sleep(5)
s.close()
print("Done.")
