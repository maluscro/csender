#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DATETIME_LENGTH 28
#define SYSLOG_MSG_MAXLENGTH 1024
#define NUM_EVENTS_IN_EVENTLIST 8

static const char* gc_event_list[ NUM_EVENTS_IN_EVENTLIST ];

int timestamp_rfc3339( char* ap_output_buffer )
{
  int to_return = -1;

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
                "%ldZ",
                ( time_spec.tv_nsec / 1000 ) );

      to_return = 0;
    }
  }

  return to_return;
}

void send_events( int a_socket )
{    
  char timestamp[ DATETIME_LENGTH ];
  char syslog_event[ SYSLOG_MSG_MAXLENGTH + 1 ];

  while( 1 )
  {
    if( timestamp_rfc3339( timestamp ) == 0 )
    {
      snprintf( syslog_event,
                SYSLOG_MSG_MAXLENGTH,
                "<13>%s localhost.localdomain my.app: %s\n",
                timestamp,
                gc_event_list[ rand() % NUM_EVENTS_IN_EVENTLIST ] );

      send( a_socket, syslog_event, strlen( syslog_event ), 0 );
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


int create_socket_and_connect_from_info_list( const struct addrinfo* ap_list )
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

        printf( "A connection with the target (%s) has been established.\n",
                ip_address );

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
        create_socket_and_connect_from_info_list( p_addrinfo_list );

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


void InitializeEventList( )
{
  gc_event_list[ 0 ] =
      "Teardown UDP connection for faddr 80.58.4.34/37074 gaddr "
      "10.0.0.187/53 laddr 192.168.0.2/53";
  gc_event_list[ 1 ] =
      "192.168.0.2 Accessed URL 212.227.109.224:/scriptlib/"
      "ClientStdScripts.js";
  gc_event_list[ 2 ] =
      "Built outbound TCP connection 152083 for faddr "
      "212.227.109.224/80 gaddr 10.0.0.187/56684 laddr "
      "192.168.0.2/56684";
  gc_event_list[ 3 ] =
      "Teardown TCP connection 151957 faddr 212.227.109.224/80 gaddr "
      "10.0.0.187/56613 laddr 192.168.0.2/56613 duration 0:04:56 "
      "bytes 11069 (TCP Reset-I)";
  gc_event_list[ 4 ] =
      "Deny TCP (no connection) from 192.168.0.2/2799 to "
      "192.168.202.1/2244 flags SYN ACK on interface inside";
  gc_event_list[ 5 ] =
      "Built UDP connection for faddr 211.9.32.235/32770 gaddr "
      "10.0.0.187/53 laddr 192.168.0.2/53";
  gc_event_list[ 6 ] =
      "Authen Session End: user '', sid 1, elapsed 313 seconds";
  gc_event_list[ 7 ] =
      "Deny icmp src outside:Some-Cisco dst inside:10.0.0.187 "
      "(type 3, code 1) by access-group \"outside_access_in\"";
}


int main( int argc, char* argv[] )
{  
  if( argc != 3 )
  {
    fprintf( stderr, "Usage: csender hostname servicename\n" );
    exit( 1 );
  }

  int to_return = -1;

  InitializeEventList( );

  // Connect to the given target
  int socket_to_target_fd = create_socket_and_connect( argv[ 1 ], argv[ 2 ] );
  if( socket_to_target_fd != -1 )
  {
    // Send events to it
    send_events( socket_to_target_fd );

    to_return = 0;
  }

  return to_return;
}
