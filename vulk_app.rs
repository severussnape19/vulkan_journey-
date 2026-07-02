#![allow(unused)]

use std::ffi::c_char;

use raw_window_handle::{HasDisplayHandle, HasWindowHandle};
use winit::{application::ApplicationHandler, event::WindowEvent, event_loop::EventLoop, window::Window};
use ash::{EntryFnV1_3, Instance, khr, vk::{self, StructureType}};

#[derive(Default)]
struct App {
    window: Option<Window>
}

struct VulkanInit {
    entry: ash::Entry,
    instance: Instance,
    surface_loader: khr::surface::Instance,
    surface: vk::SurfaceKHR,
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &winit::event_loop::ActiveEventLoop) {
        todo!()
    }

    todo!()
}

impl VulkanInit {
    fn new(window: &Window) -> Result<Self, Box<dyn std::error::Error>> {
        let entry = unsafe {
            ash::Entry::load()?
        };

        let display_handle = window.display_handle()?.as_raw();
        let mut extensions: Vec<*const c_char> = ash_window::enumerate_required_extensions(display_handle)?.to_vec();

        let app_info = vk::ApplicationInfo::default()
            .application_name(c"Vulkan application")
            .engine_name(c"Vulkan engine")
            .application_version(ash::vk::make_api_version(0, 0, 1, 0))
            .api_version(vk::API_VERSION_1_3);

        let instance_info = vk::InstanceCreateInfo::default()
            .flags(vk::InstanceCreateFlags::empty())
            .application_info(&app_info)
            .enabled_extension_names(&extensions);

        let instance = unsafe {
            entry.create_instance(&instance_info, None)?
        };

        let window_handle = window.window_handle()?.as_raw();
        let surface = unsafe {
            ash_window::create_surface(&entry, &instance, display_handle, window_handle, None)?
        };

        let surface_loader = khr::surface::Instance::new(&entry, &instance);

        println!("Created instance successfully!");
        Ok( Self {
            entry,
            instance,
            surface_loader,
            surface
        } )
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    //let app: VulkanInit = VulkanInit::new();

    Ok(())
}
