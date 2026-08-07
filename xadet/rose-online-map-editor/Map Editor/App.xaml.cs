using System.Windows;
using System;
using System.Threading;
using System.Windows.Threading;
using Map_Editor.Misc;

namespace Map_Editor
{
    /// <summary>
    /// App class.
    /// </summary>
    public partial class App : Application
    {
        #region Member Declarations

        /// <summary>
        /// Gets or sets the form.
        /// </summary>
        /// <value>The form.</value>
        public static Main Form { get; set; }

        /// <summary>
        /// Gets or sets the engine.
        /// </summary>
        /// <value>The engine.</value>
        public static Engine.Main Engine { get; set; }

        #endregion

        /// <summary>
        /// Initializes a new instance of the <see cref="App"/> class.
        /// </summary>
        /// <exception cref="T:System.InvalidOperationException">
        /// More than one instance of the <see cref="T:System.Windows.Application"/> class is created per <see cref="T:System.AppDomain"/>.
        /// </exception>
        public App()
        {
            DispatcherUnhandledException += App_DispatcherUnhandledException;
            AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;
            System.Windows.Forms.Application.ThreadException += Application_ThreadException;

            ConfigurationManager.LoadConfig();
            ConfigurationManager.CheckConfig();

            System.Windows.Forms.Application.EnableVisualStyles();

            Form = new Main();
            Form.WindowState = WindowState.Minimized;
            Form.Show();
        }

        /// <summary>
        /// Handles WPF UI thread exceptions.
        /// </summary>
        private void App_DispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
        {
            Output.WriteException("Unhandled UI exception", e.Exception);
            MessageBox.Show("The editor hit an error. Details were written to Map Editor.log.\n\n" + e.Exception.Message, "Map Editor Error", MessageBoxButton.OK, MessageBoxImage.Error);
            e.Handled = true;
        }

        /// <summary>
        /// Handles non-UI thread exceptions.
        /// </summary>
        private void CurrentDomain_UnhandledException(object sender, UnhandledExceptionEventArgs e)
        {
            Exception exception = e.ExceptionObject as Exception;

            if (exception != null)
                Output.WriteException("Unhandled application exception", exception);
            else
                Output.WriteLine(Output.MessageType.Error, "Unhandled application exception: " + e.ExceptionObject);
        }

        /// <summary>
        /// Handles WinForms-hosted control exceptions.
        /// </summary>
        private void Application_ThreadException(object sender, ThreadExceptionEventArgs e)
        {
            Output.WriteException("Unhandled WinForms exception", e.Exception);
            MessageBox.Show("The editor hit an error. Details were written to Map Editor.log.\n\n" + e.Exception.Message, "Map Editor Error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}
