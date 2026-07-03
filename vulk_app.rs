#![allow(unused)]

use std::ffi::c_char;

use ash_window::enumerate_required_extensions;
use raw_window_handle::{HasDisplayHandle, HasWindowHandle};
use winit::{application::ApplicationHandler, event::WindowEvent, event_loop::EventLoop, window::Window};
use ash::{EntryFnV1_3, Instance, khr, vk::{self, ApplicationInfo, StructureType}};

#[derive(Default)]
struct App {
    window: Option<Window>
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &winit::event_loop::ActiveEventLoop) {
        if self.window.is_none() {
            let window_attr = Window::default_attributes()
                .with_title("Vulkan ash")
                .with_inner_size(winit::dpi::LogicalSize::new(1280, 720));

            match event_loop.create_window(window_attr) {
                Ok(w) => self.window = Some(w),
                Err(e) => eprintln!("Error creating a window: {}", e),
            };
        }
    }

    fn window_event(
        &mut self,
        event_loop: &winit::event_loop::ActiveEventLoop,
        window_id: winit::window::WindowId,
        event: WindowEvent,
    ) {
        match event {
            WindowEvent::CloseRequested => {
                println!("Window close requested!");
                event_loop.exit();
            }
            WindowEvent::RedrawRequested => {
                // Vulkan draw calls
            }
            _ => {}
        }
    }
}

struct VulkanApp {
    entry: ash::Entry,
    instance: ash::Instance
}

impl VulkanApp {
    fn new(window: &winit::window::Window) -> Result<Self, Box<dyn std::error::Error>> {
        let entry = unsafe { ash::Entry::load()? };

        let app_info = vk::ApplicationInfo::default()
            .application_name(c"Vulkan ash")
            .application_version(vk::make_api_version(0, 0,  1, 0))
            .api_version(vk::API_VERSION_1_3);

        let layers: Vec<*const c_char> = vec![c"VK_LAYER_KHRONOS_validation".as_ptr()];

        // Get the raw handle
        let display_handle = window.display_handle()?.as_raw();
        let extensions = enumerate_required_extensions(display_handle)?.to_vec();

        let extension_properties = unsafe {
            entry.enumerate_instance_extension_properties(None)?
        }.to_vec();

        let mut avl_extensions: Vec<&std::ffi::CStr> = vec![];
        for e in extension_properties {
            let raw_name_ptr = e.extension_name.as_ptr();
            let ext_name = unsafe {
                std::ffi::CStr::from_ptr(raw_name_ptr)
            };
            avl_extensions.push(ext_name);
        }

        for &req_ext in &extensions {
            let req = unsafe { std::ffi::CStr::from_ptr(req_ext) };
            let is_supported = avl_extensions.iter().find(|avl_ext| **avl_ext == req);

            if is_supported.is_some() {
                return Err(format!
                    ("[ERR] Required extension is not supported by the GPU driver!: {:?}",req).into()
                );
            }
        }

        let create_info = vk::InstanceCreateInfo::default()
            .application_info(&app_info)
            .enabled_layer_names(&layers)
            .enabled_extension_names(&extensions);

        let instance = unsafe { entry.create_instance(&create_info, None)? };

        Ok(Self {
            entry,
            instance
        })
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(winit::event_loop::ControlFlow::Poll);

    let mut app = App::default();
    event_loop.run_app(&mut app)?;

    Ok(())
}
