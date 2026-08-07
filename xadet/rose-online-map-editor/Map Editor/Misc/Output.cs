using System;
using System.IO;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Threading;

namespace Map_Editor.Misc
{
    /// <summary>
    /// Output class.
    /// </summary>
    public static class Output
    {
        /// <summary>
        /// Message type
        /// </summary>
        public enum MessageType
        {
            /// <summary>
            /// Normal.
            /// </summary>
            Normal = 0,

            /// <summary>
            /// Error.
            /// </summary>
            Error = 1,

            /// <summary>
            /// Event.
            /// </summary>
            Event = 2
        }

        #region Member Declarations

        /// <summary>
        /// Gets or sets the target.
        /// </summary>
        /// <value>The target.</value>
        private static ListBox target { get; set; }

        #endregion

        /// <summary>
        /// Sets the ListBox target.
        /// </summary>
        /// <param name="target">ListBox target used to output messages.</param>
        public static void Initialize(ListBox target)
        {
            Output.target = target;
        }

        /// <summary>
        /// Output a message onto the top of the ListBox item list.
        /// </summary>
        /// <param name="messageType">Type of message to send.</param>
        /// <param name="message">Message.</param>
        public static void WriteLine(MessageType messageType, string message)
        {
            WriteToFile(string.Format("[{0:00}:{1:00}:{2:00}.{3:000}] {4}", DateTime.Now.Hour, DateTime.Now.Minute, DateTime.Now.Second, DateTime.Now.Millisecond, message));

            if (target == null)
                return;

            target.Dispatcher.Invoke(DispatcherPriority.Normal, new Action(delegate
            {
                TextBlock textBlock = new TextBlock();
                textBlock.Inlines.Add(new Run()
                {
                    Text = string.Format("[{0:00}:{1:00}:{2:00}.{3:000}] ", DateTime.Now.Hour, DateTime.Now.Minute, DateTime.Now.Second, DateTime.Now.Millisecond),
                    Foreground = new SolidColorBrush(Color.FromArgb(255, 62, 83, 120))
                });

                switch (messageType)
                {
                    case MessageType.Normal:
                        {
                            textBlock.Inlines.Add(new Run()
                            {
                                Text = message,
                                Foreground = new SolidColorBrush(Color.FromArgb(255, 62, 83, 120))
                            });
                        }
                        break;
                    case MessageType.Error:
                        {
                            textBlock.Inlines.Add(new Run()
                            {
                                Text = message,
                                Foreground = Brushes.DarkRed
                            });
                        }
                        break;
                    case MessageType.Event:
                        {
                            textBlock.Inlines.Add(new Run()
                            {
                                Text = message,
                                Foreground = new SolidColorBrush(Color.FromArgb(255, 17, 25, 38))
                            });
                        }
                        break;
                }

                target.Items.Insert(0, textBlock);
            }));
        }

        /// <summary>
        /// Writes an exception to the output log.
        /// </summary>
        /// <param name="message">Context message.</param>
        /// <param name="exception">Exception to log.</param>
        public static void WriteException(string message, Exception exception)
        {
            WriteLine(MessageType.Error, string.Format("{0}: {1}", message, exception.Message));
            WriteToFile(exception.ToString());
        }

        /// <summary>
        /// Writes a message to the editor log file.
        /// </summary>
        /// <param name="message">Message to write.</param>
        private static void WriteToFile(string message)
        {
            try
            {
                File.AppendAllText("Map Editor.log", message + Environment.NewLine);
            }
            catch
            {
            }
        }
    }
}
