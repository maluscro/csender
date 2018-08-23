#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DATETIME_LENGTH 28
#define SYSLOG_MSG_MAXLENGTH 1024
#define SYSLOG_HEADER_LENGTH 62
#define STATISTICS_INTERVAL 1
#define RANDOM_EVENT_MIN_LENGTH 100
#define RANDOM_EVENT_MAX_LENGTH 225

struct csender_arguments
{
  char*    hostname;
  char*    servicename;
  ssize_t  event_length;
};

int timestamp_rfc3339( char* ap_output_buffer,
                       bool* ap_output_second_changed_since_last_call )
{
  int to_return = -1;
  *ap_output_second_changed_since_last_call = false;

  static int last_call_second = -1;

  // Fetch no. of seconds passed since epoch, and no. of nanoseconds into the
  // next second.
  struct timespec time_spec;
  if( clock_gettime( CLOCK_REALTIME, &time_spec ) == 0 )
  {
    // Load a tm struct with those seconds
    struct tm time;
    if( localtime_r( &time_spec.tv_sec, &time ) != NULL )
    {
      // Format the output string from the tm struct
      size_t num_chars_copied = strftime( ap_output_buffer,
                                          DATETIME_LENGTH,
                                          "%FT%T.",
                                          &time);

      // Add the number of microseconds into the next second
      snprintf( &( ap_output_buffer[ num_chars_copied ] ),
                DATETIME_LENGTH - num_chars_copied,
                "%6ldZ",
                ( time_spec.tv_nsec / 1000 ) );

      // Has the second field changed since the last call?
      *ap_output_second_changed_since_last_call =
          ( ( last_call_second != time.tm_sec ) &&
            ( last_call_second >= 0 ) );

      last_call_second = time.tm_sec;
      to_return = 0;
    }
  }

  return to_return;
}


void generate_event_body( size_t a_event_length,
                          char* a_output_body )
{
  // If no body size is provided, give it a random value between
  // RANDOM_EVENT_MIN_LENGTH and RANDOM_EVENT_MAX_LENGTH.
  if( a_event_length == 0 )
  {
    a_event_length =
        RANDOM_EVENT_MIN_LENGTH +
        ( rand( ) % ( RANDOM_EVENT_MAX_LENGTH -
                      RANDOM_EVENT_MIN_LENGTH + 1 ) );
  }

  size_t body_length = a_event_length - ( SYSLOG_HEADER_LENGTH + 1 );

  // Fill the event body with a random character, between 65 ('A') and 90 ('Z')
  memset( a_output_body, 65 + ( rand( ) % 25 ), body_length );

  // Null-terminate the event
  sprintf( a_output_body + body_length, "\n%s", "\0" );
}


void generate_event( char* a_output_event,
                     const char* a_timestamp,
                     const struct csender_arguments* ap_arguments )
{
  // First add the event header
  sprintf( a_output_event,
           "<13>%s localhost.localdomain my.app: %s",
           a_timestamp,
           "\0" );
  char* a_output_event_end = a_output_event + strlen( a_output_event );

  // Then append the event body
  generate_event_body( ( ap_arguments->event_length == -1 ) ?
                         0 :
                         ( size_t ) ap_arguments->event_length,
                       a_output_event_end );
}


void send_events( int a_socket, const struct csender_arguments* ap_arguments )
{    
  // Set seed for random numbers generation
  srand( ( unsigned int ) time( NULL ) );

  char timestamp[ DATETIME_LENGTH ];
  char syslog_event[ SYSLOG_MSG_MAXLENGTH + 1 ];

  bool second_changed_since_last_timestamp = false;
  long num_second_changes = -1;
  long num_events_sent = 0;

  while( 1 )
  {
    // Generate a timestamp. Has a full second passed since the last second
    // change?
    if( timestamp_rfc3339( timestamp,
                           &second_changed_since_last_timestamp ) == 0 )
    {
      if( second_changed_since_last_timestamp )
      {
        num_second_changes++;
      }     

      if( num_second_changes >= 0 )
      {
        // Send a new event, from the just generated timestamp
        generate_event( syslog_event, timestamp, ap_arguments );
        send( a_socket, syslog_event, strlen( syslog_event ), 0 );
        num_events_sent++;
      }

      // Inform user, every few seconds...
      if( second_changed_since_last_timestamp &&
          num_second_changes >= 1 &&
          num_second_changes % STATISTICS_INTERVAL == 0 )
      {        
        printf( "%4ld sec. %10ld events sent, avg: %ld events/sec\n",
                num_second_changes,
                num_events_sent,
                num_events_sent / num_second_changes );
      }
    }
    else
    {
      printf( "It was not possible to generate a new timestamp.\n" );
      break;
    }
  }
}


void* get_in_addr( struct sockaddr* ap_socket_address )
{
  void* p_socket_address = ( void* ) ap_socket_address;

  // IPv4 or IPv6?
  if ( ap_socket_address->sa_family == AF_INET)
  {
    return &( ( ( struct sockaddr_in* ) p_socket_address )->sin_addr );
  }

  return &( ( ( struct sockaddr_in6* ) p_socket_address )->sin6_addr );
}


int create_socket_and_connect_from_info_list( const struct addrinfo* ap_list,
                                              const char* a_target_name,
                                              const char* a_service_name )
{
  int socket_fd_to_return = -1;

  // Traverse the given items, creating sockets based on them. Connect to the
  // first one that can be created.
  const struct addrinfo* p_current_addrinfo = NULL;
  for( p_current_addrinfo = ap_list;
       p_current_addrinfo != NULL;
       p_current_addrinfo = p_current_addrinfo->ai_next )
  {
    socket_fd_to_return =
        socket( p_current_addrinfo->ai_family,
                p_current_addrinfo->ai_socktype,
                p_current_addrinfo->ai_protocol );

    if( socket_fd_to_return != -1 )
    {
      if( connect( socket_fd_to_return,
                   p_current_addrinfo->ai_addr,
                   p_current_addrinfo->ai_addrlen) != -1 )
      {
        // Tell the user a connection has been established
        char ip_address[ INET6_ADDRSTRLEN ];

        inet_ntop( p_current_addrinfo->ai_family,
                   get_in_addr( (struct sockaddr *)p_current_addrinfo->ai_addr),
                   ip_address,
                   sizeof ip_address );

        printf( "\nA connection with the target (%s:%s) has been established. "
                "Sending events...\n\n",
                ( strcmp( ip_address, a_target_name ) == 0 ) ? ip_address :
                                                               a_target_name,
                a_service_name );

        // Just abandon the loop, once the socket has been created and
        // connected.
        break;
      }
      else
      {
        socket_fd_to_return = -1;
        close( socket_fd_to_return );
        perror( "It was not possible to connect to the specified target" );
      }
    }
    else
    {
      perror( "Error while creating socket" );
    }
  }

  return socket_fd_to_return;
}


int create_socket_and_connect( const char* a_target_name,
                               const char* a_service_name )
{
  int socket_fd_to_return = -1;

  // Create a 'hints' struct, in order to specify which connection endtype is
  // wanted.
  struct addrinfo hints;
  memset( &hints, 0, sizeof hints );
  hints.ai_family = AF_UNSPEC;     // Both IPv4 and IPv6 addresses are wanted
  hints.ai_socktype = SOCK_STREAM; // TCP socket

  // Fetch addrinfo items, from the hints above and the server and service names
  struct addrinfo* p_addrinfo_list = NULL;
  int error_code = getaddrinfo( a_target_name,
                                a_service_name,
                                &hints,
                                &p_addrinfo_list );

  if( error_code == 0 && p_addrinfo_list != NULL )
  {
    // Actually create a socket, and connect it to the target
    socket_fd_to_return =
        create_socket_and_connect_from_info_list( p_addrinfo_list,
                                                  a_target_name,
                                                  a_service_name );

    // Free mem storing the addrinfo items
    freeaddrinfo( p_addrinfo_list );
  }
  else
  {
    fprintf( stderr,
             "Error on getaddrinfo(): %s\n",
             gai_strerror( error_code));
  }

  return socket_fd_to_return;
}


char* trim_initial_slashes( char* a_program_name )
{
  char* program_name = a_program_name;
  char* next_slash = program_name;
  do
  {
    next_slash = strchr( program_name, '/' );

    if( next_slash != NULL )
    {
      program_name = next_slash + 1;
    }
  }
  while( next_slash != NULL );

  return program_name;
}


size_t min_event_length( )
{
  // Minimum: The syslog information + trailing \n + 1 character
  return SYSLOG_HEADER_LENGTH + 1 + 1;
}


size_t max_event_length( )
{
  return SYSLOG_MSG_MAXLENGTH;
}


void print_usage( char* a_program_name )
{
  printf( "%s. A program that sends syslog events to a receiver. Written "
          "in C using POSIX sockets.\n",
          trim_initial_slashes( a_program_name ) );
  printf( "usage:\n"
          "    csender [option]...\n"
          "options:\n"
          "    -h, --help      Print this help.\n"
          "    -H, --host      Address or name of the host to send events to. Default: 127.0.0.1.\n"
          "    -p, --port      Port or service name to send events to. Default: 8000.\n"
          "    -l, --length    Length (in chars) of the events to send [%ld-%ld].\n", min_event_length(), max_event_length() );
}


bool process_argument_list( int argc,
                            char* argv[],
                            struct csender_arguments* ap_arguments )
{
  ap_arguments->hostname = "127.0.0.1";
  ap_arguments->servicename = "8000";
  ap_arguments->event_length = -1;

  // Process options
  struct option long_options[] =
  {
  { "help", no_argument, 0, 'h' },
  { "host", required_argument, 0, 'H' },
  { "port", required_argument, 0, 'p' },
  { "length", required_argument, 0, 'l' },
  { 0, 0, 0, 0 }
  };

  int index, opt = 0;
  while( ( opt =
           getopt_long( argc, argv, "hH:p:l:", long_options, &index ) ) != -1 )
  {
    switch( opt )
    {
      case 'h':
      {
        print_usage( argv[ 0 ] );
        return false;
      }
      case 'H':
      {
        ap_arguments->hostname = optarg;
        break;
      }
      case 'p':
      {
        ap_arguments->servicename = optarg;
        break;
      }
      case 'l':
      {
        ap_arguments->event_length = atoi( optarg );

        if( ap_arguments->event_length < ( ssize_t )min_event_length( ) ||
            ap_arguments->event_length > ( ssize_t )max_event_length( ) )
        {
          printf( "Invalid event length.\n" );
          ap_arguments->event_length = -1;
          print_usage( argv[ 0 ] );
          return false;
        }

        break;
      }
      default:
      {
        printf( "Unknown option, or option without value.\n");
        print_usage( argv[ 0 ] );
        return false;
      }
    }
  }

  return true;
}


int main( int argc, char* argv[] )
{    
  struct csender_arguments arguments;
  if( process_argument_list( argc, argv, &arguments ) )
  {
    // Connect to the given target
    int socket_to_target_fd =
        create_socket_and_connect( arguments.hostname,
                                   arguments.servicename );
    if( socket_to_target_fd != -1 )
    {
      // Send events to it
      send_events( socket_to_target_fd, &arguments );
    }

    return ( socket_to_target_fd != -1 );
  }
  else
  {
    exit( 1 );
  }
}
