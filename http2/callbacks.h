#ifndef H2_CALLBACKS_H
#define H2_CALLBACKS_H

////////////////////////////////
//~ nghttp2 Callbacks

internal int h2_on_begin_headers_callback(nghttp2_session *ng_session, const nghttp2_frame *frame, void *user_data);

internal int h2_on_header_callback(nghttp2_session *ng_session, const nghttp2_frame *frame, const uint8_t *name,
                                   size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags,
                                   void *user_data);

internal int h2_on_frame_recv_callback(nghttp2_session *ng_session, const nghttp2_frame *frame, void *user_data);

internal int h2_on_data_chunk_recv_callback(nghttp2_session *ng_session, uint8_t flags, int32_t stream_id,
                                            const uint8_t *data, size_t len, void *user_data);

internal int h2_on_stream_close_callback(nghttp2_session *ng_session, int32_t stream_id, uint32_t error_code,
                                         void *user_data);

internal ssize_t h2_send_callback(nghttp2_session *ng_session, const uint8_t *data, size_t length, int flags,
                                  void *user_data);

#endif // H2_CALLBACKS_H
