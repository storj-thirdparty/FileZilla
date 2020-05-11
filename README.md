# storj-filezilla
### *Developed using RC v1.0.2 storj/uplink-c*
### *FileZilla v3.48.0*

### Download, Build and Install FileZilla with Storj (RC-v1.0.2) from source:
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
	$ curl -O https://download.filezilla-project.org/libfilezilla/libfilezilla-0.21.0.tar.bz2
	$ tar xf libfilezilla-0.21.0.tar.bz2
	$ cd libfilezilla-0.21.0
	$ autoreconf -i 
	$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static
	$ make && make install
    ```
	- Please ensure there are no empty spaces in any folder name!
* Download FileZilla source from GitHub:
    ```
    $ cd ~
	$ git clone https://github.com/storj/filezilla.git filezilla
    ```
* Generate C binding library files for Storj (RC-v1.0.2) management:
    - Please ensure [golang](https://golang.org/doc/install) is installed
    - [storj-uplink-c go package](https://github.com/storj/uplink-c )
        ```
        $ go get storj.io/uplink-c
        ```
    - In the terminal, go to the go/src/storj.io/uplink-c/ folder at your local computer
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
	

### [INFO] List of softwares, packages, libraries, and dependencies to build FileZilla with Storj (RC-v1.0.2) on Windows (64-bit)
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
    - v0.21.0

* Storj (RC-v1.0.2) Dependencies
    - [golang](https://golang.org/doc/install)
    - [storj-uplink go package](https://github.com/storj/uplink)
    - [storj-uplinkc go package](https://github.com/storj/uplink-c)

	
* **Mac OS X**:
    - Follow the instructions at [Compiling FileZilla 3 under Mac OS X](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_macOS&oldid=51125) upto (and *excluding*) ["GnuTLS"](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_macOS&oldid=51125#GnuTLS) section

	- Execute the following commands for building GnuTLS:
	```
    $ cd ~/src
    $ curl -OL ftp://ftp.gnutls.org/gcrypt/gnutls/v3.6/gnutls-3.6.7.1.tar.xz
    $ tar xvf gnutls-3.6.7.1.tar.xz
    $ cd gnutls-3.6.7
    $ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static --with-included-libtasn1 --without-p11-kit --disable-doc --enable-local-libopts --disable-nls --with-included-unistring --disable-guile
    $ make
    ```
    - **Note**: It would show various errors. Just ignore these errors and perform further steps as follows:
	```
    $ cd lib
	$ make install
    ```

    - Follow the instructions from [Compile SQLite](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_macOS&oldid=51125#Compile_SQLite) upto (and *excluding*) ["Compile wxWidgets"](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_macOS&oldid=51125#Compile_wxWidgets) section

    - Execute the following commands for building wxWidgets:
    ```
    $ cd ~/src
    $ curl -OL https://github.com/wxWidgets/wxWidgets/releases/download/v3.0.4/wxWidgets-3.0.4.tar.bz2
    $ tar xvf wxWidgets-3.0.4.tar.bz2
	$ sed -i '' "s/std::auto_ptr<CaseFolder> pcf(CaseFolderForEncoding());/std::unique_ptr<CaseFolder> pcf(CaseFolderForEncoding());/g" ~/src/wxWidgets-3.0.4/src/stc/scintilla/src/Editor.cxx
    $ cd wxWidgets-3.0.4
    $ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static --disable-webkit --disable-webview --with-macosx-version-min=10.11
    $ make
    $ make install
    ```

- Execute the following commands for building libfilezilla:
	```
    $ cd ~/src
    $ curl -OL https://download.filezilla-project.org/libfilezilla/libfilezilla-0.21.0.tar.bz2
    $ tar xf libfilezilla-0.21.0.tar.bz2
    $ cd libfilezilla-0.21.0
    $ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static
	$ make
	$ make install
    ```
	- Please ensure there are no empty spaces in any folder name!
* Download FileZilla source from GitHub:
    ```
    $ cd ~/src
	$ git clone https://github.com/storj/filezilla.git filezilla
    ```
* Generate C binding library files for Storj (RC-v1.0.2) management:
    - Please ensure [golang](https://golang.org/doc/install) is installed
    - [storj-uplink-c go package](https://github.com/storj/uplink-c )
        ```
        $ go get storj.io/uplink-c
        ```
    - In the terminal, go to the go/src/storj.io/uplink-c/ folder at your local computer
    - Generate the C binding:
        ```
        $ go build -o libuplinkc.a -buildmode=c-archive
        ```
    - Copy the generated libuplinkc.h, libuplinkc.a, and uplink_definitions.h files to the filezilla/src/storj/ folder
* Build FileZilla from source with Storj feature enable:
    ```
    $ cd ~/src/filezilla
    $ autoreconf -i
    $ ./configure --with-pugixml=builtin --enable-storj
    ```
    - **NOTE**: In *Mac OS X*, it may be required to allow execution of data/dylibcop.sh. Hence, please execute the following on the command prompt:
    ```
    $ cd data && chmod 777 dylibcopy.sh && cd ..
    $ sudo make
    ```
	
	- The generated FileZilla.app application is found within the current 'filezilla' folder itself
	
### In order to distribute the application:
- Copy the FileZilla.app within an empty folder
- Create a disk image (.dmg) file from this folder, by following instructions at [Create a disk image from a folder or connected device](https://support.apple.com/en-bh/guide/disk-utility/dskutl11888/mac)
- This disk image (.dmg file) can be shared with other Mac users

### [INFO] List of softwares, packages, libraries, and dependencies to build FileZilla with Storj (RC-v1.0.2) on MAC OS (64-bit)

* Xcode
* pkg-config
	- v0.29.2
* libidn
	- v1.35
* GMP
	- v6.1.2
* Nettle
	- v3.4.1
* GnuTLS
	- v3.6.7.1
* SQLite
	- v3.26.0
* gettext
	- v0.19.8
* wxWidgets
	- v3.0.4
* libfilezilla
	- v0.21.0

* Storj (RC-v1.0.2) Dependencies
    - [golang](https://golang.org/doc/install)
    - [storj-uplink go package](https://github.com/storj/uplink)
    - [storj-uplinkc go package](https://github.com/storj/uplink-c)
	

* **Ubuntu Linux**:
 
* Setting up build environment
    - As root, execute the following:
		- **NOTE**: Directly running the command ```apt build-dep libwxgtk3.0-dev``` (mentioned next) might display the error ```E: You must put some 'source' URIs in your sources.list``` on some systems. Hence, please execute the following on the terminal:
			```
			$ cp /etc/apt/sources.list /etc/apt/sources.list~
			$ sed -Ei 's/^# deb-src /deb-src /' /etc/apt/sources.list
			$ apt-get update
			```
		```
		$ apt build-dep libwxgtk3.0-dev
		$ apt install libtool git subversion xdg-utils libgmp-dev
		```
	- Back as normal user, execute:
		```
		$ mkdir ~/prefix
		$ export PATH="$HOME/prefix/bin:$PATH"
		$ export LD_LIBRARY_PATH="$HOME/prefix/lib:$LD_LIBRARY_PATH"
		$ export PKG_CONFIG_PATH="$HOME/prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
		```
	- **NOTE**: If you ever close the terminal and reopen it, repeat the last 3 (export) steps mentioned above before you continue.
 
* Getting and compiling dependencies
    - GMP
		```
		$ cd ~
		$ wget https://gmplib.org/download/gmp/gmp-6.1.2.tar.xz
		$ tar xvf gmp-6.1.2.tar.xz
		$ cd gmp-6.1.2
		$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static --enable-fat
		$ make && make install
		```
	- Nettle
		```
		$ cd ~
		$ wget https://ftp.gnu.org/gnu/nettle/nettle-3.4.1.tar.gz
		$ tar xf nettle-3.4.1.tar.gz
		$ cd nettle-3.4.1
		$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static --enable-fat --enable-mini-gmp
		$ make && make install
		```
	- GnuTLS
		```
		$ cd ~
		$ wget ftp://ftp.gnutls.org/gcrypt/gnutls/v3.6/gnutls-3.6.13.tar.xz
		$ tar xf gnutls-3.6.13.tar.xz
		$ cd gnutls-3.6.13
		$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static --with-included-libtasn1 --without-p11-kit --disable-doc --enable-local-libopts --disable-nls --with-included-unistring --disable-guile
		$ make && make install
		```
	- SQLite
		```
		$ cd ~
		$ wget https://sqlite.org/2018/sqlite-autoconf-3260000.tar.gz
		$ tar xvzf sqlite-autoconf-3260000.tar.gz
		$ cd sqlite-autoconf-3260000
		$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static --disable-dynamic-extensions
		$ make && make install
		```
	- wxWidgets
		```
		$ cd ~
		$ git clone --branch WX_3_0_BRANCH --single-branch https://github.com/wxWidgets/wxWidgets.git wx3
		$ cd wx3
		$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static --enable-unicode
		$ make && make install
		```
	- libfilezilla
		```
		$ cd ~
		$ wget https://download.filezilla-project.org/libfilezilla/libfilezilla-0.21.0.tar.bz2
		$ tar xf libfilezilla-0.21.0.tar.bz2
		$ cd libfilezilla-0.21.0
		$ autoreconf -i
		$ ./configure --prefix="$HOME/prefix" --enable-shared --disable-static
		$ make && make install
		```
    - Please ensure there are no empty spaces in any folder name!

* Download FileZilla source from GitHub:
    ```
	$ cd ~
    $ git clone https://github.com/storj/filezilla.git filezilla
	```
 
* Generate C binding library files for Storj (RC-v1.0.2) management:
    - Please ensure [golang](https://golang.org/doc/install) is installed
    - [storj-uplink-c go package](https://github.com/storj/uplink-c )
        ```
        $ go get storj.io/uplink-c
        ```
    - In the terminal, go to the go/src/storj.io/uplink-c/ folder at your local computer
    - Generate the C binding:
        ```
        $ go build -o libuplinkc.a -buildmode=c-archive
        ```
    - Copy the generated libuplinkc.h, libuplinkc.a, and uplink_definitions.h files to the filezilla/src/storj/ folder
 
* Build FileZilla from source with Storj feature enable:
	```
	$ cd ~/filezilla
    $ autoreconf -i
	$ ./configure --prefix="$HOME/prefix" --with-pugixml=builtin --enable-shared --disable-static --enable-storj
	$ make && make install
	```
	- The generated FileZilla binary is found in the directory ~/prefix/bin
	
* In order to run the FileZilla application, execute the following:
	```
	$ ~/prefix/bin/filezilla
	```
 
* In order to distribute the application:
	```
	$ strip --strip-debug $HOME/prefix/lib/*.so
	$ strip --strip-debug $HOME/prefix/bin/*
	```
	- **NOTE**: Ignore the error ```strip:/home/...../prefix/bin/wx-config: File format not recognized```
	```
	$ cd ~
	$ tar cvfj filezilla.tar.bz2 prefix
	```
	
* In order to run the application on another Ubuntu system:
	- Copy ```filezilla.tar.bz2``` to ```$HOME``` folder of another Ubuntu system
	- Execute the following:
		```
		$ cd ~
		$ tar xf filezilla.tar.bz2
		$ export PATH="$HOME/prefix/bin:$PATH"
		$ export LD_LIBRARY_PATH="$HOME/prefix/lib:$LD_LIBRARY_PATH"
		$ export PKG_CONFIG_PATH="$HOME/prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
		$ ~/prefix/bin/filezilla
		```
	- **NOTE**: If you ever close the terminal and reopen it, repeat the 3 ```export``` steps mentioned above before you continue.

### [INFO] List of softwares, packages, libraries, and dependencies to build FileZilla with Storj (RC-v1.0.2) on Linux Ubuntu (64-bit)

* libtool
* git
* subversion
* xdg-utils
* GMP
	- v6.1.2
* Nettle
	- v3.4.1
* GnuTLS
	- v3.6.13
* SQLite
	- v3.26.0
* wxWidgets
	- v3.0.5
* libfilezilla
	- v0.21.0
 
#### References:
* [Compiling FileZilla 3 under Windows](https://wiki.filezilla-project.org/wiki/index.php?title=Compiling_FileZilla_3_under_Windows&oldid=51076)
* [Compiling FileZilla 3 under macOS](https://wiki.filezilla-project.org/Compiling_FileZilla_3_under_macOS)
* [Create a disk image from a folder or connected device](https://support.apple.com/en-bh/guide/disk-utility/dskutl11888/mac)
* [About Storj connection with FileZilla](https://wiki.filezilla-project.org/Storj)
* [Storj (RC-v1.0.2) uplink-c](https://github.com/storj/uplink-c)
 
### Testing
* The project has been tested on the following operating systems:

 * Windows 10 Pro
    * Processor: Intel(R) Core(TM) i3-5005U CPU @ 2.00GHz 2.00GHz

 * macOS Catalina
    * Version: 10.15.4
    * Processor: 2.5 GHz Dual-Core Intel Core i5

 * Ubuntu
	* Version: 18.04.4 LTS (64-bit)
	* Processor: AMD A6-7310 apu with amd radeon r4 graphics x 4
