//! Container knowledge that belongs to one disc rather than to a format.
//!
//! The *parsers* for these containers are in `psxrt-import`. What is here is the surrounding
//! knowledge a parser must not hold: which filenames on this disc behave differently from the
//! others, and why.

pub mod mrg;
