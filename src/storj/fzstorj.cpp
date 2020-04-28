#include <stdio.h>
#include <ctime>

#include "events.hpp"

#include <libfilezilla/format.hpp>
#include <libfilezilla/mutex.hpp>

typedef bool _Bool;
#include "libuplinkc.h"

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

#include "require.h"

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

extern "C" void fv_listBuckets(Project *project)
{
	BucketIterator *it = list_buckets(project, NULL);

	int count = 0;
	while (bucket_iterator_next(it)) {
		Bucket *bucket = bucket_iterator_item(it);
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
		free_bucket(bucket);
		count++;
	}
	Error *err = bucket_iterator_err(it);
	if (err) {
		fzprintf(storjEvent::Error, "bucket listing failed: %s", err->message);
		free_error(err);
		free_bucket_iterator(it);
		return;
	}	
		
	free_bucket_iterator(it);
}

extern "C" void fv_listObjects(Project *project, std::string bucket, std::string prefix)
{			
	ObjectIterator *it;
	ListObjectsOptions options;
	
	if(prefix.empty()) {
		options = {
			system : true,
			custom : true,
		};	
	}
	else {
		prefix = prefix + "/";
		options = {
			prefix : const_cast<char*>(prefix.c_str()),
			system : true,
			custom : true,
		};
	}
	
	it = list_objects(project, const_cast<char*>(bucket.c_str()), &options);
	
	int count = 0;
	while (object_iterator_next(it)) {
		Object *object = object_iterator_item(it);
		
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
				
		free_object(object);
		count++;
	}
			
	Error *err = object_iterator_err(it);
	if (err) {
		fzprintf(storjEvent::Error, "object listing failed: %s", err->message);
		free_error(err);
		free_object_iterator(it);
		return;
	}
	free_object_iterator(it);
}

extern "C" void fv_downloadObject(Project *project, std::string bucket, std::string id, std::string file)
{
	DownloadResult download_result = download_object_custom(project, const_cast<char*>(bucket.c_str()), const_cast<char*>(id.c_str()), const_cast<char*>(file.c_str()), NULL);
	if (download_result.error) {
		fzprintf(storjEvent::Error, "download starting failed: %s", download_result.error->message);
		free_download_result(download_result);
		return;
	}

	fzprintf(storjEvent::Status, "downloaded object %s", id);

    Download *download = download_result.download;
	
	ObjectResult object_result = download_info(download);
    require_noerror(object_result.error);
    require(object_result.object != NULL);
	
	Object *object = object_result.object;
	
	fzprintf(storjEvent::Transfer, "%u", object->system.content_length);

	free_download_result(download_result);	
}

extern "C" void fv_uploadObject(Project *project, std::string bucket, std::string prefix, std::string file, std::string objectName)
{
	std::string object_key = bucket;
	if(prefix != "")
		object_key = object_key + "/" + prefix;

	UploadResult upload_result = upload_object_custom(project, const_cast<char*>(object_key.c_str()), const_cast<char*>(file.c_str()), const_cast<char*>(objectName.c_str()), NULL);
	if (upload_result.error) {
		fzprintf(storjEvent::Status, "upload starting failed: %s", upload_result.error->message);
		free_upload_result(upload_result);
		return;
	}

	fzprintf(storjEvent::Status, "uploaded object %s", objectName);
	
    Upload *upload = upload_result.upload;

	ObjectResult object_result = upload_info(upload);
    require_noerror(object_result.error);
    require(object_result.object != NULL);
	
	Object *object = object_result.object;
	
	fzprintf(storjEvent::Transfer, "%u", object->system.content_length);	

	free_upload_result(upload_result);
}

extern "C" void fv_deleteObject(Project *project, std::string bucketName, std::string objectKey)
{	
	ObjectResult object_result = delete_object(project, const_cast<char*>(bucketName.c_str()), const_cast<char*>(objectKey.c_str()));
	require_noerror(object_result.error);
	require(object_result.object != NULL);
	
	fzprintf(storjEvent::Status, "deleted object %s", objectKey);
	free_object_result(object_result);
}

extern "C" void fv_createBucket(Project *project, std::string bucketName)
{
	BucketResult bucket_result = ensure_bucket(project, const_cast<char*>(bucketName.c_str()));
	if (bucket_result.error) {
		fzprintf(storjEvent::Error, "failed to create bucket %s: %s", bucketName, bucket_result.error->message);
		free_bucket_result(bucket_result);
		return;
	}
   
	Bucket *bucket = bucket_result.bucket;
	fzprintf(storjEvent::Status, "created bucket %s", bucket->name);
	free_bucket_result(bucket_result);
}

extern "C" void fv_deleteBucket(Project *project, std::string bucketName)
{
	BucketResult bucket_result = delete_bucket(project, const_cast<char*>(bucketName.c_str()));
	require_noerror(bucket_result.error);
	require(bucket_result.bucket != NULL);
			
	Bucket *bucket = bucket_result.bucket;
	fzprintf(storjEvent::Status, "deleted bucket %s", bucket->name);
	free_bucket_result(bucket_result);
}

}

int main()
{
	fzprintf(storjEvent::Reply, "fzStorj started, protocol_version=%d", FZSTORJ_PROTOCOL_VERSION);

	std::string ls_satelliteURL;
	std::string ls_apiKey;
	std::string ls_encryptionPassPhrase;
	std::string ls_serializedAccessGrantKey;
		
	auto fv_openStorjProject = [&]() -> ProjectResult {
		AccessResult access_result;
		if(!(ls_apiKey.empty())) {
			access_result = request_access_with_passphrase(const_cast<char*>(ls_satelliteURL.c_str()), const_cast<char*>(ls_apiKey.c_str()), const_cast<char*>(ls_encryptionPassPhrase.c_str()));
			if (access_result.error) {
				fzprintf(storjEvent::Error, "failed to parse access: %s", access_result.error->message);
				free_access_result(access_result);
			}
		}
		else {
			access_result = parse_access(const_cast<char*>(ls_serializedAccessGrantKey.c_str()));
			if (access_result.error) {
				fzprintf(storjEvent::Error, "failed to parse access: %s", access_result.error->message);
				free_access_result(access_result);
			}
		}
		
		ProjectResult project_result = open_project(access_result.access);
		if (project_result.error) {
			fzprintf(storjEvent::Error, "failed to open project: %s", project_result.error->message);
			free_project_result(project_result);
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
			ProjectResult project_result = fv_openStorjProject();
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

			ProjectResult project_result = fv_openStorjProject();			
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
			size_t pos2 = arg.find(' ', pos + 1);
			if (pos == std::string::npos) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			auto id = arg.substr(pos + 1, pos2 - pos - 1);
			auto file = arg.substr(pos2 + 1);

			if (file.size() >= 3 && file.front() == '"' && file.back() == '"') {
				file = fz::replaced_substrings(file.substr(1, file.size() - 2), "\"\"", "\"");
			}
			
			ProjectResult project_result = fv_openStorjProject();
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

			ProjectResult project_result = fv_openStorjProject();
			fv_uploadObject(project_result.project, bucket, prefix, file, objectName);

			// refresh
			fv_listObjects(project_result.project, bucket, prefix);

			fzprintf(storjEvent::Done);
		}
		else if (command == "rm") {
			auto args = fz::strtok(arg, ' ');
			if (args.size() != 2) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}

			std::string bucketName = args[0];
			std::string objectKey = args[1];
			std::string prefix = "";
			
			size_t pos = objectKey.find_last_of('/');
			if (pos != std::string::npos) {
				prefix = objectKey.substr(0, pos);
			}	
			
			ProjectResult project_result = fv_openStorjProject();
			fv_deleteObject(project_result.project, bucketName, objectKey);

			// refresh
			project_result = fv_openStorjProject();
			fv_listObjects(project_result.project, bucketName, prefix);
			
			fzprintf(storjEvent::Done);	
		}
		else if (command == "mkbucket") {
			std::string bucketName = next_argument(arg);
			if (bucketName.empty()) {
				fzprintf(storjEvent::Error, "Bad arguments");
				continue;
			}
			
			ProjectResult project_result = fv_openStorjProject();
			fv_createBucket(project_result.project, bucketName);
						
			// refresh
			fv_listBuckets(project_result.project);
			
			fzprintf(storjEvent::Done);		
		}
		else if (command == "rmbucket") {
			std::string bucketName = arg;
					
			ProjectResult project_result = fv_openStorjProject();
			fv_deleteBucket(project_result.project, bucketName);
			
			// refresh
			fv_listBuckets(project_result.project);
	
			fzprintf(storjEvent::Done);
		}
		else {
			fzprintf(storjEvent::Error, "No such command: %s", command);
		}

	}

	return ret;}