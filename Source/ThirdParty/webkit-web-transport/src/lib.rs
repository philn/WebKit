use quinn_proto::{ClientConfig, Connection, ConnectionHandle, Endpoint, EndpointConfig};
use rand::RngCore;
use ring::hmac;
use std::net::SocketAddr;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Instant;

struct WKQuinnEndPoint(Endpoint);
pub struct WKQuinnConnection {
    connection: Connection,
    handle: ConnectionHandle,
}

#[cxx::bridge(namespace = "org::webkit")]
mod ffi {
    extern "Rust" {
        type WKQuinnEndPoint;
        fn create_endpoint() -> Box<WKQuinnEndPoint>;

        type WKQuinnConnection;
        fn create_connection(
            self: &mut WKQuinnEndPoint,
            url: String,
        ) -> Result<Box<WKQuinnConnection>>;
    }
}

fn create_endpoint() -> Box<WKQuinnEndPoint> {
    let mut key_material = vec![0; 64];
    let mut rng = rand::rng();
    rng.fill_bytes(&mut key_material);
    let reset_key = Arc::new(hmac::Key::new(hmac::HMAC_SHA256, &key_material));

    let config = Arc::new(EndpointConfig::new(reset_key));
    let endpoint = Endpoint::new(config, None, false, None);
    Box::new(WKQuinnEndPoint(endpoint))
}

impl WKQuinnEndPoint {
    fn create_connection(&mut self, url: String) -> anyhow::Result<Box<WKQuinnConnection>> {
        let config = ClientConfig::with_platform_verifier();
        let addr: SocketAddr = url.parse()?;
        let (handle, connection) = self.0.connect(Instant::now(), config, addr, "foo")?;
        Ok(Box::new(WKQuinnConnection {
            connection: connection,
            handle: handle,
        }))
    }
}
