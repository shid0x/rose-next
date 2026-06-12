# ROSE Next Launcher Tools
A collection of tools for the ROSE Next Launcher and archive builder

This project contains the ROSE Next Launcher which is a GUI for the user to
receive news about the game and patch theri client. This project also includes a
tool used to generate the archives in the format used by the launcher.

The ROSE Next launcher uses a unique method of patching the client. Rather than
manually creating each patch with the changelist, we simply maintain the latest
version of the client and the launcher syncs it with the remote archive¹

¹ Thanks for the bitar library by Olle Sandberg for making this feature possible (https://github.com/oll3/bita)

## Usage
- Create an archive using the archiver command
- Host the archive using a webserver that supports HTTP range requests (https://developer.mozilla.org/en-US/docs/Web/HTTP/Range_requests)
- Configure the launcher to point to the webserver
- Start the launcher