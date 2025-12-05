# DHFS4_1

A X-Ways Forensics X-Tension to parse the DAHUA filesystem 'DHFS4.1'.

# Usage

After importing the filesystem image (E01, DD etc.) into a case rightclick on the virtual file which represents the unknown filesystem.
Load the X-Tension (DHFS4_1.dll or whatever you want to name it) and execute it.

<img width="682" height="553" alt="Screenshot 2025-12-05 073626" src="https://github.com/user-attachments/assets/72753a82-5111-438a-b7c2-f212a7ef14e9" />

Now all video files should be visible in the volume snapshot.

<img width="376" height="147" alt="Screenshot 2025-12-05 073859" src="https://github.com/user-attachments/assets/8013b12c-831b-4f29-8cb9-c4e07197c5b8" />

After that, close the opened image and open it via disk I/O with the X-Tension (DHFS4_1.dll) to trigger the disk I/O X-Tension functions. 

<img width="344" height="319" alt="Screenshot 2025-12-05 073657" src="https://github.com/user-attachments/assets/398351ab-d690-4435-8010-437e11a7bc6e" />

It will once again search the whole disk. This is neccessary because the program need to know all the locations and offsets in the filesystem. Now, the fragmented files can be accessed.

I recommend to read the paper which you can find in this GitHub repository. It's in german for now, I'm planning to translate it into english.

X-Tension is tested on version 21.4 SR-5

© 2025 Dane Wullen

NO WARRANTY, SOFWARE IS PROVIDED 'AS IS'
