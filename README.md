# Nginy

<p align="center">
  <img src="./banner_v1.png" alt="Nginy Logo" />
</p>

**A minimalist, high performance and non-blocking web server written in ~400 lines of C.**
Nginy is for people who love the simplicity of static websites.

# What is a webserver?

If we strip down all the details, a webserver is just a program that listens to messages from other program, and then answers them accordingly.

**That is exactly what Nginy does:**
1. Listen for a connection on PORT (default 8080)
2. Parse and sanitize the request. 
3. Stream the file back to the client.
4. Go back to Step 1.

Then when we tried to send request from 1000 clients, the 1000th client had to wait a really long time for the server to serve them, that is not how modern websites behave. We need to have concurrency. Luckily, the linux kernel have `epoll` and `sendfile`, we implemented that.

# Install

```bash
git clone https://github.com/cnyegun/nginy
cd nginy
make
./nginy
```

You will need to create a directory named public/ inside nginy/, that's where you put your website files in.
