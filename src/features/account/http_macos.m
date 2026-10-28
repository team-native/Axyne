#include "axyne/http.h"

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AxyneStatus http_error(AxyneError *error, AxyneStatus code, const char *message)
{
    if (error != NULL) {
        error->code = code;
        (void)snprintf(error->message, sizeof(error->message), "%s", message);
    }
    return code;
}

void axyne_http_response_clear(AxyneHttpResponse *response)
{
    if (response == NULL) return;
    free(response->body);
    memset(response, 0, sizeof(*response));
}

AxyneStatus axyne_http_request(const char *method, const char *url, const char *bearer,
                               const char *json_body, AxyneHttpResponse *response, AxyneError *error)
{
    NSAutoreleasePool *pool;
    NSURL *address;
    NSMutableURLRequest *request;
    NSURLSessionConfiguration *configuration;
    NSURLSession *session;
    dispatch_semaphore_t finished;
    __block NSData *received = nil;
    __block NSInteger code = 0;
    __block BOOL failed = NO;
    AxyneStatus status = AXYNE_STATUS_OK;

    if (method == NULL || url == NULL || response == NULL)
        return http_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid HTTP request");
    memset(response, 0, sizeof(*response));
    pool = [[NSAutoreleasePool alloc] init];
    {
        NSString *text = [NSString stringWithUTF8String:url];
        address = text != nil ? [NSURL URLWithString:text] : nil;
    }
    if (address == nil || [NSString stringWithUTF8String:method] == nil ||
        (bearer != NULL && [NSString stringWithUTF8String:bearer] == nil) ||
        (bearer != NULL && strpbrk(bearer, "\r\n") != NULL)) {
        [pool release];
        return http_error(error, AXYNE_STATUS_INVALID_ARGUMENT, "invalid server address");
    }
    request = [NSMutableURLRequest requestWithURL:address];
    [request setHTTPMethod:[NSString stringWithUTF8String:method]];
    [request setTimeoutInterval:AXYNE_HTTP_TIMEOUT_MS / 1000.0];
    [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
    if (bearer != NULL)
        [request setValue:[@"Bearer " stringByAppendingString:[NSString stringWithUTF8String:bearer]] forHTTPHeaderField:@"Authorization"];
    if (json_body != NULL) {
        [request setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        [request setHTTPBody:[NSData dataWithBytes:json_body length:strlen(json_body)]];
    }
    configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    session = [NSURLSession sessionWithConfiguration:configuration];
    finished = dispatch_semaphore_create(0);
    [[session dataTaskWithRequest:request
                completionHandler:^(NSData *data, NSURLResponse *reply, NSError *taskError) {
        if (taskError != nil) failed = YES;
        else {
            received = [data retain];
            if ([reply isKindOfClass:[NSHTTPURLResponse class]]) code = [(NSHTTPURLResponse *)reply statusCode];
        }
        dispatch_semaphore_signal(finished);
    }] resume];
    dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
    dispatch_release(finished);
    [session finishTasksAndInvalidate];

    if (failed) {
        status = http_error(error, AXYNE_STATUS_IO_ERROR, "the server could not be reached");
    } else {
        size_t length = received != nil ? (size_t)[received length] : 0;
        response->body = (char *)malloc(length + 1);
        if (response->body == NULL) {
            status = http_error(error, AXYNE_STATUS_OUT_OF_MEMORY, "out of memory");
        } else {
            if (length > 0) memcpy(response->body, [received bytes], length);
            response->body[length] = '\0';
            response->length = length;
            response->status = (int)code;
            if (error != NULL) { error->code = AXYNE_STATUS_OK; error->message[0] = '\0'; }
        }
    }
    [received release];
    [pool release];
    return status;
}
