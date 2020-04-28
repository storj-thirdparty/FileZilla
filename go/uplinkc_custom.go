// Copyright (C) 2020 Storj Labs, Inc.
// See LICENSE for copying information.

package main

// #include "uplink_definitions.h"
import "C"
import (
	"bytes"
	"io"
	"io/ioutil"
	"time"
	"storj.io/uplink"
)

//export download_object_custom
// download_object_custom starts  download to the specified key.
func download_object_custom(project *C.Project, bucket_name, object_key, destFullFileName *C.char, options *C.DownloadOptions) C.DownloadResult { //nolint:golint
	if project == nil {
		return C.DownloadResult{
			error: mallocError(ErrNull.New("project")),
		}
	}
	if bucket_name == nil {
		return C.DownloadResult{
			error: mallocError(ErrNull.New("bucket_name")),
		}
	}
	if object_key == nil {
		return C.DownloadResult{
			error: mallocError(ErrNull.New("object_key")),
		}
	}

	proj, ok := universe.Get(project._handle).(*Project)
	if !ok {
		return C.DownloadResult{
			error: mallocError(ErrInvalidHandle.New("project")),
		}
	}
	scope := proj.scope.child()

	opts := &uplink.DownloadOptions{
		Offset: 0,
		Length: -1,
	}
	if options != nil {
		opts.Offset = int64(options.offset)
		opts.Length = int64(options.length)
	}

	download, err := proj.DownloadObject(scope.ctx, C.GoString(bucket_name), C.GoString(object_key), opts)
	if err != nil {
		return C.DownloadResult{
			error: mallocError(err),
		}
	}

	// Read everything from the download stream
	receivedContents, err := ioutil.ReadAll(download)
	if err != nil {
		return C.DownloadResult{
			error: mallocError(err),
		}
	}
	
	// Write the content to the desired file
	err = ioutil.WriteFile(C.GoString(destFullFileName), receivedContents, 0644)
	if err != nil {
		return C.DownloadResult{
			error: mallocError(err),
		}
	}
	
	return C.DownloadResult{
		download: (*C.Download)(mallocHandle(universe.Add(&Download{scope, download}))),
	}	
}

//export upload_object_custom
// upload_object_custom starts an upload to the specified key.
func upload_object_custom(project *C.Project, bucket_name, srcFullFileName, path *C.char, options *C.UploadOptions) C.UploadResult { //nolint:golint
	if project == nil {
		return C.UploadResult{
			error: mallocError(ErrNull.New("project")),
		}
	}
	if bucket_name == nil {
		return C.UploadResult{
			error: mallocError(ErrNull.New("bucket_name")),
		}
	}
	if path == nil {
		return C.UploadResult{
			error: mallocError(ErrNull.New("path")),
		}
	}

	proj, ok := universe.Get(project._handle).(*Project)
	if !ok {
		return C.UploadResult{
			error: mallocError(ErrInvalidHandle.New("project")),
		}
	}
	scope := proj.scope.child()

	opts := &uplink.UploadOptions{}
	if options != nil {
		if options.expires > 0 {
			opts.Expires = time.Unix(int64(options.expires), 0)
		}
	}

	upload, err := proj.UploadObject(scope.ctx, C.GoString(bucket_name), C.GoString(path), opts)
	if err != nil {
		return C.UploadResult{
			error: mallocError(err),
		}
	}

	dataToUpload, err := ioutil.ReadFile(C.GoString(srcFullFileName))
	if err != nil {
		return C.UploadResult{
			error: mallocError(err),
		}
	}

	// Copy the data to the upload.
	buf := bytes.NewBuffer(dataToUpload)
	_, err = io.Copy(upload, buf)
	if err != nil {
		_ = upload.Abort()
		//return fmt.Errorf("could not upload data: %v", err)
		return C.UploadResult{
			error: mallocError(err),
		}
	}

	// Commit the uploaded object.
	err = upload.Commit()
	if err != nil {
		//return fmt.Errorf("could not commit uploaded object: %v", err)
		return C.UploadResult{
			error: mallocError(err),
		}
	}

	return C.UploadResult{
		upload: (*C.Upload)(mallocHandle(universe.Add(&Upload{scope, upload}))),
	}
}