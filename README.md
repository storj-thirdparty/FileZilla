# storj-filezilla
### *Developed using RC v1.0.1 storj/uplink-c*
### *FileZilla v3.43.0*

### Download, Build and Install FileZilla with Storj (RC-v1.0.1) from source:
* **Windows 10**:
    - Follow the instructions at [Compiling FileZilla 3 under Windows](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076 ) upto (and *excluding*) ["Setting up the environment"](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076#Setting_up_the_environment ) section
	- Execute the following commands for setting up the environment:
	```
    $ mkdir ~/prefix
	$ echo 'export PATH="$HOME/prefix/bin:$HOME/prefix/lib:/mingw64/bin:/mingw32/bin:$PATH"' >> ~/.profile
	$ echo 'export PKG_CONFIG_PATH="$HOME/prefix/lib/pkgconfig"' >> ~/.profile
	$ echo 'export PATH="$HOME/prefix/bin:$HOME/prefix/lib:/mingw64/bin:/mingw32/bin:$PATH"' >> ~/.bash_profile
	$ echo 'export PKG_CONFIG_PATH="$HOME/prefix/lib/pkgconfig"' >> ~/.bash_profile
    ```
	- Restart the MSYS2 MINGW64 shell.
	- Follow the instructions at [Building dependencies](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076#Building_dependencies ) upto (and *excluding*) ["Building libfilezilla"](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076#Building_libfilezilla ) section
	- Execute the following commands for building libfilezilla:
	```
    $ cd ~
	$ curl -O https://download.filezilla-project.org/libfilezilla/libfilezilla-0.20.2.tar.bz2
	$ tar xf libfilezilla-0.20.2.tar.bz2
	$ cd libfilezilla-0.20.2
	$ autoreconf -i 
	$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static
	$ make && make install
    ```
	- Please ensure there are no empty spaces in any folder name!
* Download FileZilla source from github:
    ```
    $ cd ~
    $ git clone https://github.com/utropicmedia/storj-filezilla.git filezilla
    ```
* Generate C binding library files for Storj (RC-v1.0.1) management:
    - Please ensure [golang](https://golang.org/doc/install) is installed
    - [storj-uplink-c go package](https://github.com/storj/uplink-c )
    ```
    $ go get storj.io/uplink-c
    ```
    - Copy the uplinkc_custom.go file (to be found in just downloaded filezilla/go/ folder) within the $HOME/go/src/storj.io/uplink-c/ folder at your local computer
    - Generate the C binding:
    ```
    $ go build -o libuplinkc.a -buildmode=c-archive
    ```
    - Copy the generated libuplinkc.h, libuplinkc.a, and uplink_definitions.h files to the filezilla/src/storj/ folder in MSYS2
* Build FileZilla from source with Storj feature enable:
	```
    $ cd ~/filezilla
	$ autoreconf -i
	$ ./configure --with-pugixml=builtin --enable-storj
	$ make
    ```
	
### In order to distribute the application:
- Follow the instructions at [Stripping debug symbols](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076#Stripping_debug_symbols) upto ["Compile the installer script"](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076#Compile_the_installer_script ) section
	

### [INFO] List of softwares, packages, libraries, and dependencies to build FileZilla with Storj (RC-v1.0.1) on Windows (64-bit)
* autoconf
* automake
* libtool
* make
* git
* svn
* gmp 
    - v6.2.0
* nettle
    - v3.5.1
* zlib
    - v1.2.11
* gnutls
    - v3.6.13
* sqlite
    - v3.25.3
* wxWidgets
    - v3.0.5
* libfilezilla
    - v0.20.2

* Storj (RC-v1.0.1) Dependencies
    - The repository already contains certain libraries (libuplinkc.a, libuplinkc.h) and header files (uplink_definitions.h), generated/taken from the following:
        - [golang](https://golang.org/doc/install)
        - [storj-uplink go package](https://github.com/storj/uplink )
        - [storj-uplinkc go package](https://github.com/storj/uplink-c )

#### References:
* [Compiling FileZilla 3 under Windows](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076)
* [About Storj connection with FileZilla](https://wiki.filezilla-project.org/Storj)
* [Storj (RC-v1.0.1) uplink-c](https://github.com/storj/uplink-c)

### Testing
* The project has been tested on the following operating systems:
 * Windows 10 Pro
    * Processor: Intel(R) Core(TM) i3-5005U CPU @ 2.00GHz 2.00GHz