#include "rcheevos_integration.h"

#include <rc_client.h>

#include "common/logging/log.h"

rc_client_t* g_client = NULL;

// This is the function the rc_client will use to read memory for the emulator. we don't need it yet,
// so just provide a dummy function that returns "no memory read".
static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t* client)
{
  // TODO: implement later
  LOG_DEBUG(Rcheevos, "Attempting to read memory.");

  return 0;
}

// // This is the callback function for the asynchronous HTTP call (which is not provided in this example)
// static void http_callback(int status_code, const char* content, size_t content_size, void* userdata, const char* error_message)
// {
//   // Prepare a data object to pass the HTTP response to the callback
//   rc_api_server_response_t server_response;
//   memset(&server_response, 0, sizeof(server_response));
//   server_response.body = content;
//   server_response.body_length = content_size;
//   server_response.http_status_code = status_code;

//   // handle non-http errors (socket timeout, no internet available, etc)
//   if (status_code == 0 && error_message) {
//       // assume no server content and pass the error through instead
//       server_response.body = error_message;
//       server_response.body_length = strlen(error_message);
//       // Let rc_client know the error was not catastrophic and could be retried. It may decide to retry or just
//       // immediately pass the error to the callback. To prevent possible retries, use RC_API_SERVER_RESPONSE_CLIENT_ERROR.
//       server_response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
//   }

//   // Get the rc_client callback and call it
//   async_callback_data* async_data = (async_callback_data*)userdata;
//   async_data->callback(&server_response, async_data->callback_data);

//   // Release the captured rc_client callback data
//   free(async_data);
// }

// This is the HTTP request dispatcher that is provided to the rc_client. Whenever the client
// needs to talk to the server, it will call this function.
static void server_call(const rc_api_request_t* request,
    rc_client_server_callback_t callback, void* callback_data, rc_client_t* client)
{
  LOG_DEBUG(Rcheevos, "Attempting to call server.");

  // // RetroAchievements may not allow hardcore unlocks if we don't properly identify ourselves.
  // const char* user_agent = "MyClient/1.2";

  // // callback must be called with callback_data, regardless of the outcome of the HTTP call.
  // // Since we're making the HTTP call asynchronously, we need to capture them and pass it
  // // through the async HTTP code.
  // async_callback_data* async_data = malloc(sizeof(async_callback_data));
  // async_data->callback = callback;
  // async_data->callback_data = callback_data;

  // // If post data is provided, we need to make a POST request, otherwise, a GET request will suffice.
  // if (request->post_data)
  //   async_http_post(request->url, request->post_data, user_agent, http_callback, async_data);
  // else
  //   async_http_get(request->url, user_agent, http_callback, async_data);
}

// Write log messages to the console
static void log_message(const char* message, const rc_client_t* client)
{
  LOG_DEBUG(Rcheevos, "Rcheevos internal message: \"{}\"", message);
}

void initialize_retroachievements_client()
{
  LOG_DEBUG(Rcheevos, "Initializing RA client.");

  // Create the client instance (using a global variable simplifies this example)
  g_client = rc_client_create(read_memory, server_call);

  // Provide a logging function to simplify debugging
  rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message);

  // Disable hardcore - if we goof something up in the implementation, we don't want our
  // account disabled for cheating.
  rc_client_set_hardcore_enabled(g_client, 0);
}

void shutdown_retroachievements_client()
{
  LOG_DEBUG(Rcheevos, "Shutting down RA client.");

  if (g_client)
  {
    // Release resources associated to the client instance
    rc_client_destroy(g_client);
    g_client = NULL;
  }
}