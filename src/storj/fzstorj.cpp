#include <stdio.h>
#include <fstream>
#include <ctime>

#include "events.hpp"

#include <libfilezilla/format.hpp>
#include <libfilezilla/mutex.hpp>

typedef bool _Bool;
//#include "libuplinkc.h"
#include <storj/libuplinkc.h>
#include "require.h"

#include <map>

fz::mutex output_mutex;

void fzprintf(storjEvent event)
{
	fz::scoped_lock l(output_mutex);

	fputc('0' + static_cast<int>(event), stdout);

	fflush(stdout);
}

template<typename ...Args>
void fzprintf(storjEvent event, Args &&... args)
{
	fz::scoped_lock l(output_mutex);

	fputc('0' + static_cast<int>(event), stdout);

	std::string s = fz::sprintf(std::forward<Args>(args)...);
	fwrite(s.c_str(), s.size(), 1, stdout);

	fputc('\n', stdout);
	fflush(stdout);
}

bool getLine(std::string & line)
{
	line.clear();
	while (true) {
		int c = fgetc(stdin);
		if (c == -1) {
			return false;
		}
		else if (!c) {
			return line.empty();
		}
		else if (c == '\n') {
			return !line.empty();
		}
		else if (c == '\r') {
			continue;
		}
		else {
			line += static_cast<unsigned char>(c);
		}
	}
}

std::string next_argument(std::string & line)
{
	std::string ret;

	fz::trim(line);

	if (line[0] == '"') {
		size_t pos = 1;
		size_t pos2;
		while ((pos2 = line.find('"', pos)) != std::string::npos && line[pos2 + 1] == '"') {
			ret += line.substr(pos, pos2 - pos + 1);
			pos = pos2 + 2;
		}
		if (pos2 == std::string::npos || (line[pos2 + 1] != ' ' && line[pos2 + 1] != '\0')) {
			line.clear();
			ret.clear();
		}
		ret += line.substr(pos, pos2 - pos);
		line = line.substr(pos2 + 1);
	}
	else {
		size_t pos = line.find(' ');
		if (pos == std::string::npos) {
			ret = line;
			line.clear();
		}
		else {
			ret = line.substr(0, pos);
			line = line.substr(pos + 1);
		}
	}

	fz::trim(line);

	return ret;
}

namespace {
#define DEBUG_MODE false

extern "C" void fv_listBuckets(Uplink_Project *project)
{
	Uplink_BucketIterator *it = uplink_list_buckets(project, NULL);

	int count = 0;
	while (uplink_bucket_iterator_next(it)) {
		Uplink_Bucket *bucket = uplink_bucket_iterator_item(it);
		std::string id = bucket->name;
		std::string name = bucket->name;
		char lc_a1_dateTime[64];
		if (bucket->created != 0) {
			std::time_t l_epoch = bucket->created;
			std::strftime(lc_a1_dateTime, 64, "%Y/%m/%d %I:%M:%S %p", std::gmtime(&l_epoch));
		}
		fz::replace_substrings(name, "\r", "");
		fz::replace_substrings(id, "\r", "");
		fz::replace_substrings(name, "\n", "");
		fz::replace_substrings(id, "\n", "");
		auto perms = "id:" + id;
		fzprintf(storjEvent::Listentry, "%s\n-1\n%s\n%s", name, perms, lc_a1_dateTime);
		uplink_free_bucket(bucket);
		count++;
	}
	Uplink_Error *err = uplink_bucket_iterator_err(it);
	if (err) {
		fzprintf(storjEvent::Error, "bucket listing failed: %s", err->message);
		uplink_free_error(err);
		uplink_free_bucket_iterator(it);
		return;
	}	
		
	uplink_free_bucket_iterator(it);
}

extern "C" void fv_listObjects(Uplink_Project *project, std::string bucket, std::string prefix)
{			
	if(!(prefix.empty()))
		prefix = prefix + "/";
	
	Uplink_ListObjectsOptions options = {
		//.prefix = const_cast<char*>(prefix.c_str()),
		.prefix = prefix.c_str(),
		.cursor = "",
		.recursive = false, 
		.system = true,
		.custom = true,
	};

	Uplink_ObjectIterator *it = uplink_list_objects(project, const_cast<char*>(bucket.c_str()), &options);

	int count = 0;
	while (uplink_object_iterator_next(it)) {
		Uplink_Object *object = uplink_object_iterator_item(it);
		
		char lc_a1_dateTime[64];
		if (object->system.created != 0) {
			std::time_t l_epoch = object->system.created;
			std::strftime(lc_a1_dateTime, 64, "%Y/%m/%d %I:%M:%S %p", std::gmtime(&l_epoch));
		}
			
		std::string objectName = object->key;
		if(!prefix.empty()) {
			size_t pos = objectName.find(prefix);
			if (pos != std::string::npos) {
				objectName = objectName.substr(pos+prefix.size(), objectName.size());
			}
		}

		if (prefix.empty()) {
			fzprintf(storjEvent::Listentry, "%s\n%d\nid:%s\n%s", objectName, object->system.content_length, objectName, lc_a1_dateTime);
		} 
		else {
			fzprintf(storjEvent::Listentry, "%s\n%d\nid:%s%s\n%s", objectName, object->system.content_length, prefix, objectName, lc_a1_dateTime);
		}
				
		uplink_free_object(object);
		count++;
	}
			
	Uplink_Error *err = uplink_object_iterator_err(it);
	if (err) {
		fzprintf(storjEvent::Error, "object listing failed: %s", err->message);
		uplink_free_error(err);
		uplink_free_object_iterator(it);
		return;
	}
	uplink_free_object_iterator(it);
}

extern "C" void fv_downloadObject(Uplink_Project *project, std::string bucket, std::string id, std::string file)
{
	Uplink_DownloadResult download_result = uplink_download_object(project, const_cast<char*>(bucket.c_str()), const_cast<char*>(id.c_str()), NULL);
    if (download_result.error) {
        fzprintf(storjEvent::Error, "download starting failed: %s", download_result.error->message);
        uplink_free_download_result(download_result);
        return;
    }

	size_t buffer_size = 32768;
    char *buffer = static_cast<char*>(malloc(buffer_size));
	std::ofstream outfile (file, std::ofstream::binary);

    Uplink_Download *download = download_result.download;
	
	size_t downloaded_total = 0;
    
    while (true) {
        Uplink_ReadResult result = uplink_download_read(download, buffer, buffer_size);
        downloaded_total += result.bytes_read;
		
		outfile.write(buffer, result.bytes_read);
				
		fzprintf(storjEvent::Transfer, "%u", result.bytes_read);

        if (result.error) {
            if (result.error->code == EOF) {
                uplink_free_read_result(result);
                break;
            }
            fzprintf(storjEvent::Error, "download failed to read: %s", result.error->message);
            uplink_free_read_result(result);
            return;
        }
        uplink_free_read_result(result);
    }

    Uplink_Error *close_error = uplink_close_download(download);
    if (close_error) {
        fzprintf(storjEvent::Error, "download failed to close: %s", close_error->message);
        uplink_free_error(close_error);
    }

    uplink_free_download_result(download_result);
}

extern "C" void fv_uploadObject(Uplink_Project *project, std::string bucket, std::string prefix, std::string file, std::string objectName)
{
	std::string object_key = bucket;
	if(prefix != "")
		object_key = object_key + "/" + prefix;

	std::ifstream is (file, std::ifstream::binary);
  
    // get length of file:
    is.seekg (0, is.end);
	size_t length = is.tellg();
    is.seekg (0, is.beg);

	size_t buffer_size = 32768;
    char *buffer = static_cast<char*>(malloc(buffer_size));
    
    Uplink_UploadResult upload_result = uplink_upload_object(project, const_cast<char*>(object_key.c_str()), const_cast<char*>(objectName.c_str()), NULL);
    
	require_noerror(upload_result.error);
    require(upload_result.upload->_handle != 0);

    Uplink_Upload *upload = upload_result.upload;

    size_t uploaded_total = 0;

	while (uploaded_total < length) {
		is.read (buffer, buffer_size);
		Uplink_WriteResult result = uplink_upload_write(upload, buffer, is.gcount());
        uploaded_total += result.bytes_written;
        
		fzprintf(storjEvent::Transfer, "%u", result.bytes_written);

		require_noerror(result.error);
        require(result.bytes_written > 0);
        uplink_free_write_result(result);
    }

    Uplink_Error *commit_err = uplink_upload_commit(upload);
    require_noerror(commit_err);

    uplink_free_upload_result(upload_result);
}

extern "C" void fv_deleteObject(Uplink_Project *project, std::string bucketName, std::string objectKey)
{	
	Uplink_ObjectResult object_result = uplink_delete_object(project, const_cast<char*>(bucketName.c_str()), const_cast<char*>(objectKey.c_str()));
	require_noerror(object_result.error);
	require(object_result.object != NULL);
	
	fzprintf(storjEvent::Status, "deleted object %s", objectKey);
	uplink_free_object_result(object_result);
}

extern "C" void fv_createBucket(Uplink_Project *project, std::string bucketName)
{
	Uplink_BucketResult bucket_result = uplink_ensure_bucket(project, const_cast<char*>(bucketName.c_str()));
	if (bucket_result.error) {
		fzprintf(storjEvent::Error, "failed to create bucket %s: %s", bucketName, bucket_result.error->message);
		uplink_free_bucket_result(bucket_result);
		return;
	}
   
	Uplink_Bucket *bucket = bucket_result.bucket;
	fzprintf(storjEvent::Status, "created bucket %s", bucket->name);
	uplink_free_bucket_result(bucket_result);
}

extern "C" void fv_deleteBucket(Uplink_Project *project, std::string bucketName)
{
	Uplink_BucketResult bucket_result = uplink_delete_bucket(project, const_cast<char*>(bucketName.c_str()));
	require_noerror(bucket_result.error);
	require(bucket_result.bucket != NULL);
			
	Uplink_Bucket *bucket = bucket_result.bucket;
	fzprintf(storjEvent::Status, "deleted bucket %s", bucket->name);
	uplink_free_bucket_result(bucket_result);
}

}

int main()
{
	fzprintf(storjEvent::Reply, "fzStorj started, protocol_version=%d", FZSTORJ_PROTOCOL_VERSION);

	std::string ls_satelliteURL;
	std::string ls_apiKey;
	std::string ls_encryptionPassPhrase;
	std::string ls_serializedAccessGrantKey;
	
	Uplink_Config config = {
        .user_agent = "FileZilla",
    };
	
	////////
	Uplink_ProjectResult project_result;
	////////

	auto fv_openStorjProject = [&]() -> Uplink_ProjectResult {
		Uplink_AccessResult access_result;
		if(!(ls_apiKey.empty())) {
			access_result = uplink_config_request_access_with_passphrase(config, const_cast<char*>(ls_satelliteURL.c_str()), const_cast<char*>(ls_apiKey.c_str()), const_cast<char*>(ls_encryptionPassPhrase.c_str()));
			if (access_result.error) {
				fzprintf(storjEvent::Error, "failed to parse access: %s", access_result.error->message);
				uplink_free_access_result(access_result);
			}
		}
		else {
			access_result = uplink_parse_access(const_cast<char*>(ls_serializedAccessGrantKey.c_str()));
			if (access_result.error) {
				fzprintf(storjEvent::Error, "failed to parse access: %s", access_result.error->message);
				uplink_free_access_result(access_result);
			}
		}
		
		Uplink_ProjectResult project_result = uplink_config_open_project(config, access_result.access);
		if (project_result.error) {
			fzprintf(storjEvent::Error, "failed to open project: %s", project_result.error->message);
			uplink_free_project_result(project_result);
		}
		return project_result;
	};
	
	int ret = 0;
	while (true) {
		std::string command;
		if (!getLine(command)) {
			ret = 1;
			break;
		}

		if (command.empty()) {
			break;
		}
		
		std::size_t pos = command.find(' ');
		std::string arg;
		if (pos != std::string::npos) {
			arg = command.substr(pos + 1);
			command = command.substr(0, pos);
		}

		if (command == "host") {
			ls_serializedAccessGrantKey = ls_satelliteURL = arg;
			fzprintf(storjEvent::Done);
		}
		else if (command == "user") {
			ls_apiKey = arg;
			//
			fzprintf(storjEvent::Done);
		}
		else if (command == "pass") {
			ls_encryptionPassPhrase = arg;
			fzprintf(storjEvent::Done);
		}
		else if (command == "genkey") {
			fzprintf(storjEvent::Done, "");
		}
		else if (command == "key" || command == "validatekey") {
			if (command == "key") {
				fzprintf(storjEvent::Done);
			}
			else {
				fzprintf(storjEvent::Done, "");
			}
		}
		else if (command == "timeout") {
			// timeout = fz::to_integral<uint64_t>(arg);
			fzprintf(storjEvent::Done);
		}
		else if (command == "proxy") {
			fzprintf(storjEvent::Done);
		}
		else if (command == "list-buckets") {
			//Uplink_ProjectResult project_result = fv_openStorjProject();
			project_result = fv_openStorjProject();
			fv_listBuckets(project_result.project);
			
			fzprintf(storjEvent::Done);
		}
		else if (command == "list") {
			
			if (arg.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			std::string bucket, prefix;

			size_t pos = arg.find(' ');
			if (pos == std::string::npos) {
				bucket = arg;
			}
			else {
				bucket = arg.substr(0, pos);

				prefix = arg.substr(pos + 1);

				if (prefix.size() >= 2 && prefix.front() == '"' && prefix.back() == '"') {
					prefix = fz::replaced_substrings(prefix.substr(1, prefix.size() - 2), "\"\"", "\"");
				}

				if (!prefix.empty() && prefix.back() != '/') {
					fzprintf(storjEvent::Error, "Bad arguments");
					continue;
				}
			}
			
			if (!prefix.empty()) {
				size_t pos = prefix.find_last_of('/');
				//
				if (pos != std::string::npos) {
					prefix = prefix.substr(0, pos);
				}
			}

			//Uplink_ProjectResult project_result = fv_openStorjProject();			
			fv_listObjects(project_result.project, bucket, prefix);
			
			fzprintf(storjEvent::Done);			
		}
		else if (command == "get") {
			size_t pos = arg.find(' ');
			if (pos == std::string::npos) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			std::string bucket = arg.substr(0, pos);
			std::string others = arg.substr(pos + 1, arg.size());
			size_t pos2 = others.find_first_of(' "');

			auto id = others.substr(0, pos2 - 1);
			auto file = others.substr(pos2, others.size());

			if (file.size() >= 3 && file.front() == '"' && file.back() == '"') {
				file = fz::replaced_substrings(file.substr(1, file.size() - 2), "\"\"", "\"");
			}

			//Uplink_ProjectResult project_result = fv_openStorjProject();
			project_result = fv_openStorjProject();
			fv_downloadObject(project_result.project, bucket, id, file);
			
			fzprintf(storjEvent::Done);			
		}
		else if (command == "put") {
			std::string bucket = next_argument(arg);
			std::string file = next_argument(arg);
			std::string remote_name = next_argument(arg);

			if (bucket.empty() || file.empty() || remote_name.empty() || !arg.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
			}
			
			if (file == "null" ) {
				file = "";
			}

			std::string prefix="";
			std::string objectName="";
			
			size_t pos = remote_name.find_last_of('/');
			if (pos != std::string::npos) {
				prefix = remote_name.substr(0, pos);
				objectName = remote_name.substr(pos+1,remote_name.size());
			}
			else {
				objectName = remote_name;
			}

			//Uplink_ProjectResult project_result = fv_openStorjProject();
			project_result = fv_openStorjProject();
			fv_uploadObject(project_result.project, bucket, prefix, file, objectName);

			// refresh
			//fv_listObjects(project_result.project, bucket, prefix);

			fzprintf(storjEvent::Done);
		}
		else if (command == "rm") {

			size_t space_pos = arg.find_first_of(' ');
			
			std::string bucketName = arg.substr(0, space_pos);
			std::string objectKey = arg.substr(space_pos+1, arg.size());
			std::string prefix = "";
			
			size_t pos = objectKey.find_last_of('/');
			if (pos != std::string::npos) {
				prefix = objectKey.substr(0, pos);
			}	
			
			//Uplink_ProjectResult project_result = fv_openStorjProject();
			fv_deleteObject(project_result.project, bucketName, objectKey);

			// refresh
			//project_result = fv_openStorjProject();
			//fv_listObjects(project_result.project, bucketName, prefix);
			
			fzprintf(storjEvent::Done);	
		}
		else if (command == "mkbucket") {
			std::string bucketName = next_argument(arg);
			if (bucketName.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			
			//Uplink_ProjectResult project_result = fv_openStorjProject();
			fv_createBucket(project_result.project, bucketName);
						
			// refresh
			fv_listBuckets(project_result.project);
			
			fzprintf(storjEvent::Done);		
		}
		else if (command == "rmbucket") {
			std::string bucketName = arg;
					
			//Uplink_ProjectResult project_result = fv_openStorjProject();
			fv_deleteBucket(project_result.project, bucketName);
			
			// refresh
			fv_listBuckets(project_result.project);
	
			fzprintf(storjEvent::Done);
		}
		else {
			fzprintf(storjEvent::Error, "No such command: %s", command);
		}

	}

	return ret;
}
