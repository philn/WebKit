//use futures::channel::oneshot;
use quinn::Endpoint;
use std::sync::Arc;
use std::sync::Mutex;
use url::Url;
use web_transport_quinn;

// struct WKQuinnEndPoint(Endpoint);
struct WKQuinnClient(Arc<web_transport_quinn::Client>);
pub struct WKQuinnSession(web_transport_quinn::Session);

//struct WKQuinnSessionFuture;

// The inner type is the Rust type that this future yields.
// #[cxx_async::bridge]
// unsafe impl Future for WKQuinnSessionFuture {
//     type Output = WKQuinnSession;
// }

#[cxx::bridge(namespace = "org::webkit")]
mod ffi {
    extern "Rust" {
        //         // type WKQuinnEndPoint;
        //         // fn create_endpoint() -> Result<Box<WKQuinnEndPoint>>;

        //         type WKQuinnClient;
        //         fn create_client() -> Box<WKQuinnClient>;

        type WKQuinnSession;
        //         type WKQuinnSessionFuture;
        //         fn create_session(self: &WKQuinnClient, url: String) -> Result<()>;
    }

    //     // extern "C++" {
    //     //     type CompletionHandler;
    //     // }
}

// // fn create_endpoint() -> anyhow::Result<Box<WKQuinnEndPoint>> {
// //     let endpoint = quinn::Endpoint::client("[::]:0".parse()?)?;
// //     Ok(Box::new(WKQuinnEndPoint(endpoint)))
// // }

// fn create_client() -> Box<WKQuinnClient> {
//     Box::new(WKQuinnClient(Arc::new(web_transport_quinn::Client::new())))
// }

// impl WKQuinnClient {
//     async fn create_session(
//         &self,
//         url_string: String,
//         // completion_handler: CompletionHandler,
//     ) -> Result<()> {
//         Ok(())
//         // let (tx, rx) = oneshot::channel();
//         // let url = Url::parse(&url_string)?;
//         // //let client = Arc::clone(&self.0);
//         // let session = self.0.connect(&url).await?;
//         // Ok(Box::new(session))
//     }
// }
