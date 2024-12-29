use quinn::Endpoint;

struct WKQuinnEndPoint(Endpoint);

#[cxx::bridge(namespace = "org::webkit")]
mod ffi {
    extern "Rust" {
        type WKQuinnEndPoint;
        fn create_endpoint() -> Box<WKQuinnEndPoint>;
    }
}

fn create_endpoint() -> Box<WKQuinnEndPoint> {
    let mut endpoint = quinn::Endpoint::client("[::]:0".parse().unwrap()).unwrap();
    //endpoint.set_default_client_config(/* ... */);
    return Box::new(WKQuinnEndPoint(endpoint));
}
