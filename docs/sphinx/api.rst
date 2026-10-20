C++ API reference
=================

These declarations are extracted from the same headers used by the build.
Implementation and test files are available in the source catalogue.

Framing
-------

.. doxygenclass:: chat::protocol::FrameError

.. doxygenclass:: chat::protocol::FrameDecoder

.. doxygenfunction:: chat::protocol::encode_frame

.. doxygenvariable:: chat::protocol::kMaxFrameSize

.. doxygenfunction:: chat::protocol::new_message_key

.. doxygenfunction:: chat::protocol::valid_message_key

.. doxygenfunction:: chat::protocol::message_timestamp

Headless client
---------------

.. doxygenclass:: chat::client::PingClient

.. doxygentypedef:: chat::client::ChatClient

.. doxygentypedef:: chat::client::ClientEvent

.. doxygenenum:: chat::client::RequestError

.. doxygenstruct:: chat::client::LoginResult

.. doxygenstruct:: chat::client::OperationResult

.. doxygenstruct:: chat::client::DirectDelivery

.. doxygenstruct:: chat::client::GroupDelivery

.. doxygenstruct:: chat::client::UserSnapshot

.. doxygenstruct:: chat::client::GroupInfo

.. doxygenstruct:: chat::client::GroupSnapshot

.. doxygenstruct:: chat::client::HistoryRecord

.. doxygenstruct:: chat::client::HistoryPage

.. doxygenclass:: chat::client::NetworkError

.. doxygenstruct:: chat::client::SyncPage

.. doxygenstruct:: chat::client::DirectRoomInfo

.. doxygenstruct:: chat::client::DirectRoomsPage

Room model and networking worker
--------------------------------

.. doxygenstruct:: chat::client::ChatLine

.. doxygenstruct:: chat::client::RoomView

.. doxygenclass:: chat::client::RoomModel

.. doxygenenum:: chat::client::CommandKind

.. doxygenstruct:: chat::client::WorkerLimits

.. doxygenstruct:: chat::client::WorkerConnected

.. doxygenstruct:: chat::client::WorkerReconnecting

.. doxygenstruct:: chat::client::WorkerDirectRoom

.. doxygenstruct:: chat::client::WorkerDisconnected

.. doxygenstruct:: chat::client::WorkerCompleted

.. doxygentypedef:: chat::client::WorkerUpdate

.. doxygenclass:: chat::client::ChatWorker

TLS transport configuration
---------------------------

.. doxygenclass:: chat::transport::TlsError

.. doxygenstruct:: chat::transport::ClientTlsOptions

.. doxygenstruct:: chat::transport::ServerTlsOptions

.. doxygenstruct:: chat::transport::ClientAppOptions

.. doxygenfunction:: chat::transport::client_app_options

.. doxygenclass:: chat::transport::SocketStream

.. doxygenfunction:: chat::transport::server_tls_context

.. doxygenfunction:: chat::transport::client_tls_context

.. doxygenfunction:: chat::transport::verify_tls_peer

Console client
--------------

.. doxygenclass:: chat::cli::ChatConsole

.. doxygenclass:: chat::cli::LineInput

.. doxygenclass:: chat::cli::ConsoleInput

.. doxygenstruct:: chat::cli::InputChunk

.. doxygenstruct:: chat::cli::InputLine

.. doxygenstruct:: chat::cli::Command

.. doxygenenum:: chat::cli::Action

.. doxygenfunction:: chat::cli::parse_command

.. doxygenfunction:: chat::cli::escape_text

Server
------

.. doxygenstruct:: chat::server::SessionLimits

.. doxygenclass:: chat::server::ClientSession

.. doxygenclass:: chat::server::PingServer

.. doxygenclass:: chat::server::ChatHub

.. doxygenstruct:: chat::server::StoredIdentity

.. doxygenstruct:: chat::server::StoredGroup

.. doxygenstruct:: chat::server::StoredMessage

.. doxygenclass:: chat::server::MessageIdConflict

.. doxygenclass:: chat::server::HistoryStore
